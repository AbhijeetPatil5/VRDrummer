# VRDrummer — Design Document

**Version:** 1.0  
**Last updated:** June 2026  
**Repository:** [github.com/AbhijeetPatil5/VRDrummer](https://github.com/AbhijeetPatil5/VRDrummer)  
**Status:** Authoritative architecture, implementation, and learning reference

Specific hardware used for development lives in [HARDWARE.md](HARDWARE.md). Everything below is **hardware-agnostic**.

---

## Table of Contents

1. [Vision, Scope, and Nomenclature](#1-vision-scope-and-nomenclature)
2. [Minimum Capability Requirements](#2-minimum-capability-requirements)
3. [System Architecture](#3-system-architecture)
4. [Cross-Cutting Infrastructure](#4-cross-cutting-infrastructure)
5. [Transport and Codecs](#5-transport-and-codecs)
6. [Interface Contracts](#6-interface-contracts)
7. [Technology Stack](#7-technology-stack)
8. [Build Roadmap](#8-build-roadmap)
9. [Performance and Concurrency](#9-performance-and-concurrency)
10. [Latency and Bandwidth Budget](#10-latency-and-bandwidth-budget)
11. [Implementation Reference](#11-implementation-reference)
12. [Glossary, AI Tutor Protocol, and Document History](#12-glossary-ai-tutor-protocol-and-document-history)

---

## 1. Vision, Scope, and Nomenclature

### Product Vision

VRDrummer teaches drumming in mixed reality. The player wears a passthrough VR headset,
sees their real electronic drum kit, and receives timed glow cues on each pad. Strikes come
from real sticks (USB MIDI — not VR controllers). The player loads a drum chart (`.mid`),
hits pads in time, and receives scoring feedback.

### Two MVPs (build in this order)

**Perception MVP** — prove the kit is seen and glows align (no chart logic):

- Live camera frames decoded on PC
- All pads detected and localised
- Stable poses via `IPoseFilter`
- Glow circles over real pads in passthrough

**Product MVP** — full tutor experience:

- Load `.mid` chart → timed cues → hit judgment → score → song library

Perception MVP is milestone **M22**. Product MVP is milestone **M40**.

### Nomenclature

| Term | Meaning | Example |
|------|---------|---------|
| **Phase** | Roadmap stage grouping milestones | Phase 2 = Video streaming |
| **Milestone** | One deliverable with acceptance tests | M4 = USB camera stream |
| **Tier** | Technology complexity ceiling (not schedule) | Tier 2 = GPU decode + TensorRT |

### Design constraints (do not assume otherwise)

| Claim | Fact |
|-------|------|
| Raw PCA pixels available via OpenXR passthrough on PC | **False** — compositor only; need companion stream |
| Companion encode is software H.264 | **False** — already HW MediaCodec; Tier 2 = HEVC + tuning |
| Headset API provides stereo extrinsics | **False** — need checkerboard calibration |
| ORT IOBinding == TensorRT `enqueueV3` | **False** — different APIs |
| CV subtotal ~45–55 ms = glass-to-glow | **Partial** — full loop ~70–90 ms typical |

---

## 2. Minimum Capability Requirements

| Role | Minimum capability |
|------|-------------------|
| **PC CPU** | ≥ 8 cores; thread pinning recommended |
| **PC GPU** | Hardware H.264 decode; HEVC decode for Tier 2; CUDA or chosen compute API; ≥ 8 GB VRAM |
| **PC OS** | Windows 10+ or Linux with OpenXR runtime |
| **Headset** | OpenXR passthrough; RGB cameras via platform API; tethered PC link for MR |
| **Drum kit** | USB (or platform) MIDI; pad count configurable via kit profile |
| **Network** | Gigabit LAN for optional Wi-Fi transport path |

Kit pad names and MIDI note numbers: `config/kits/<kit_name>.yaml` — not hardcoded in code.

---

## 3. System Architecture

### Layer stack

```text
L5 Output     — MR glows, HUD, audio, menus
L4 Logic      — Chart engine, scheduler, hit judge, scorer
L3 State      — IPoseFilter, PadPoseStore, TransformTree
L2 Processing — Decode, CV, track, 3D localise
L1 Input      — Video transport, MIDI
L0 Platform   — OpenXR, graphics API, passthrough compositor
```

**Poses stay in PC RAM.** MR display sends composited overlays to the headset only — no video back for the real-world view.

### Data flows

| Flow | Direction | Payload |
|------|-----------|---------|
| CV video | Headset → PC | Compressed camera frames (~8–20 Mbps) |
| MR display | PC → Headset | Passthrough + glow geometry via link |
| MIDI | Kit → PC | Note-On events |
| Pad poses | PC internal | N × `{position, normal, radius}` |

### Transform tree

```text
HeadsetTrackingFrame → WorldFrame (OpenXR stage/local)
LeftCameraFrame, RightCameraFrame → WorldFrame (metadata + calibration)
DrumKitFrame → WorldFrame (kit detector or user calib)
PadFrame[i] → DrumKitFrame
```

Probe camera intrinsics and resolution at runtime — never hardcode focal length or baseline.

### Pipeline overview (conceptual)

```text
[Capture] → [Encode H.264/HEVC] → [Transport] → [Decode] → [Preprocess]
  → [Kit ROI] → [Pad seg] → [Track] → [3D] → [IPoseFilter] → [Glow render]
```

Tier details: [§7](#7-technology-stack). Code patterns: [§11](#11-implementation-reference).
Latency: [§10](#10-latency-and-bandwidth-budget). Threading: [§9](#9-performance-and-concurrency).

---

## 4. Cross-Cutting Infrastructure

Built in **Phase 3** (M10–M12) before heavy perception work.

### Unified time domain

Monotonic `timestamp_ns` (`std::chrono::steady_clock`) on every stage:

```text
Capture → Encode → Transport → Decode → Infer → Estimate → Render → MIDI → Judge
```

Each frame carries a `LatencyTrace` of `StageTimestamp` pairs. Health monitor and §10 budgets derive from this.

### Configuration

`config/config.yaml` — thresholds, filter type, ports, judgment windows. Kit maps in `config/kits/*.yaml`.

### Record / replay / simulation

| Module | Role |
|--------|------|
| `VideoRecorder` | Elementary stream + index |
| `MetadataRecorder` | Per-frame JSON |
| `MidiRecorder` | Timestamped hits |
| `ReplaySession` | Implements `IVideoTransport` from disk |
| `BenchmarkRunner` | Batch replay → CSV metrics |

Simulation = full CV/estimation pipeline without headset attached.

### Health monitor

ImGui panel: dropped frames, decode errors, stereo sync offset, queue depths, glass-to-glow, MIDI-to-flash.

### Repository layout

```text
VRDrummer/
├── companion/          # Headset streaming apps
├── calibration/
├── config/
├── recording/
├── songs/
├── src/
│   ├── core/           # TimeStamp, TransformTree, HealthMonitor, RingBuffer
│   ├── stream/         # IVideoTransport, IDecoder
│   ├── cv/             # IKitDetector, IDetector, ITracker
│   ├── estimation/     # IPoseFilter
│   ├── music/ gameplay/ xr/ render/ audio/ ui/ midi/
├── docs/DESIGN.md
├── docs/HARDWARE.md
├── midiTest.cpp
├── vrTest.cpp
└── CMakeLists.txt
```

---

## 5. Transport and Codecs

Build **USB first**. Wi-Fi only after USB acceptance (M9).

### Path A — USB (primary)

| Layer | Tier 1 | Tier 2+ |
|-------|--------|---------|
| Capture | Platform passthrough camera service | Same |
| Encode | HW **H.264** | HW **HEVC** + low-latency keys |
| Transport | TCP via USB debug tunnel | Same; optional UDP/RTP |
| Metadata | Separate TCP port (JSON) | Optional dedicated UDP |
| PC decode | FFmpeg CPU | NVDEC zero-copy |
| Tuning | `TCP_NODELAY`, small recv buffer | Same |

Metadata JSON includes `"codec": "h264" | "hevc"` for decoder selection.

### Path B — Wi-Fi (secondary)

WebRTC SRTP; **H.264 Constrained Baseline only** (`profile-level-id=42e01f`); jitter buffer disabled on PC receiver. Keep wired MR link during Wi-Fi CV tests.

HEVC on Wi-Fi is **deferred** — poor WebRTC interop vs H.264 CBP.

### Codec strategy (H.264 vs HEVC)

| Path | Tier 1 | Tier 2 |
|------|--------|--------|
| USB bootstrap | H.264 | — |
| USB target | — | HEVC (~30–40% bandwidth savings for stereo) |
| Wi-Fi | H.264 CBP | H.264 until HEVC WebRTC proven |

**HEVC acceptance (M7):** companion emits `video/hevc`; PC decodes with `hevc_cuvid`; latency ≤ H.264 + 5 ms; metadata `codec` field matches.

Stereo fallback: 15 fps per eye if 30 fps stereo overloads encoder (pads are quasi-static).

Implementation: [§11.1](#111-video-decode).

---

## 6. Interface Contracts

Swappable backends — pipeline code never calls vendor APIs directly.

```cpp
class IVideoTransport {
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool readFrame(Frame& out) = 0;  // latest-frame-wins
};
// UsbTcpTransport | WifiWebRtcTransport | ReplaySession

class IDecoder {
    virtual bool decode(const Packet& in, Frame& out) = 0;
};
// CpuFfmpegDecoder | NvdecDecoder

class IKitDetector {
    virtual bool detect(const Frame& in, cv::Rect& roi) = 0;
};
// ContourKitDetector | YoloKitDetector

class IDetector {
    virtual std::vector<PadDetection> detect(const Frame& in, const cv::Rect& roi) = 0;
};
// YoloOnnxDetector | TensorRtDetector

class ITracker {
    virtual std::vector<TrackedPad> update(const std::vector<PadDetection>& dets) = 0;
};
// ClassLockTracker | ByteTrackTracker

class IPoseFilter {
    virtual void predict(double dt) = 0;
    virtual void update(const TrackedPad& m) = 0;
    virtual PadPose getPose(int pad_id) const = 0;
};
// FixedPoseFilter | EkfPoseFilter | UkfPoseFilter
```

`Frame`: `GpuMat` or `Mat` BGR, `timestamp_ns`, `CameraId`, optional `codec`.

---

## 7. Technology Stack

### Tier map

| Tier | Milestones | Ceiling |
|------|------------|---------|
| 1 Bootstrap | M4, M10–M14 | CPU decode, H.264, ONNX, EMA tracker |
| 2 Performance | M7–M8, M15–M22 | HEVC USB, NVDEC, TRT, ByteTrack, CLAHE |
| 3 Transport | M9 | Wi-Fi WebRTC |
| 4 Product | M23–M40 | Charts, scoring, UI, audio |

### PC stack

| Area | Tier 1 | Tier 2+ | Portable alternative |
|------|--------|---------|----------------------|
| Build | CMake ≥ 3.20, C++17 | Same | Same |
| VR | OpenXR + passthrough ext | + color space, hand tracking | Any OpenXR runtime |
| Graphics | D3D11 (Windows link) | Same | Vulkan via OpenXR (future) |
| Decode | FFmpeg CPU | NVDEC / vendor HW decode | Always keep CPU fallback |
| CV | OpenCV CPU | OpenCV CUDA | CPU path always valid |
| Infer | ONNX Runtime CUDA EP | TensorRT FP16→INT8 | ORT CPU for debug |
| Filter | Eigen EKF | + UKF compare | Same |
| MIDI | RtMidi | Same | Platform MIDI API |
| Audio | — | miniaudio | SoLoud also valid |
| Charts | — | midifile (M23) | libsmf alternative |
| Debug | ImGui, nlohmann/json | + HealthMonitor | Same |
| Labels | CVAT → YOLO-seg | Same | Same |

### Headset companion (reference)

Tier 1: mono H.264 stream app. Tier 2: HEVC stereo fork. Wi-Fi: Unity/WebRTC companion (reference only).

Export (on target GPU): `yolo export format=onnx` → `format=engine half=True` → `int8=True` if needed.

---

## 8. Build Roadmap

Flat milestones **M1–M40**. Phases group them. Each phase opens with a **Learning Block**; each milestone includes **what you build** (logic sketch) and **acceptance**.

**Estimates:** Phases 1–5 (through M22) ≈ 6–10 months. Phases 6–10 ≈ 6–10 months after.

---

### Phase 1 — Platform Foundation

**Goal:** PC talks to drum kit (MIDI) and headset (OpenXR).

#### Learning objectives

- Read C++ that uses callbacks and RAII
- Explain MIDI Note-On vs velocity
- Describe OpenXR instance → session → frame loop
- Bind a graphics API to OpenXR

#### Prerequisites

- C++: functions, classes, pointers, `std::vector`, CMake `add_executable` / `target_link_libraries`
- MIDI: [RtMidi tutorial](https://github.com/thestk/rtmidi) — focus on `RtMidiIn` + callback
- OpenXR: Khronos intro — instance, system, extensions (read only for M2)

#### If you are stuck

| Symptom | Check | Next step |
|---------|-------|-----------|
| No MIDI devices | OS MIDI settings, USB cable | Run `midiTest` with port list |
| OpenXR instance fails | PC VR runtime installed, headset connected | Print `xrResultToString` |
| Extension not found | Runtime version, extension name spelling | Load proc with `xrGetInstanceProcAddr` |

#### Milestones

**M1 — MIDI capture** (Done)

```text
open RtMidiIn → enumerate ports → openPort(index, callback)
callback(note, velocity):
    pad_name = kit_map[note]   // from YAML kit profile
    print(pad_name, velocity)
```

| Acceptance | All configured pads print correct name on hit |

**M2 — OpenXR probe** (Partial)

```text
xrCreateInstance(extensions=[XR_FB_passthrough, XR_KHR_D3D11_enable])
xrGetSystem → xrGetInstanceProcAddr("xrCreatePassthroughFB")
verify proc non-null
```

| Acceptance | Instance + passthrough proc loads; no session yet |

**M3 — Session and render loop**

```text
create D3D11 device → xrCreateSession(graphicsBinding)
create passthrough layer
loop:
    xrWaitFrame → xrBeginFrame
    xrLocateSpace / xrLocateViews
    draw test quad at fixed world position
    xrEndFrame(passthrough + projection layers)
```

| Acceptance | Passthrough + test quad, 72+ Hz, 5 min stable |

---

### Phase 2 — Video Streaming

**Goal:** Camera frames on PC; optional HEVC, NVDEC, Wi-Fi.

#### Learning objectives

- Why a companion app is needed (passthrough ≠ raw PC pixels)
- H.264 NAL units vs HEVC VPS/SPS/PPS
- TCP tunneling and non-blocking recv
- Stereo timestamps and extrinsics calibration

#### Prerequisites

- FFmpeg: demux/decode tutorial (`avcodec_send_packet` / `receive_frame`)
- Sockets: TCP client, `TCP_NODELAY`
- Linear algebra: pinhole camera, epipolar geometry (for M6)

#### If you are stuck

| Symptom | Check | Next step |
|---------|-------|-----------|
| Black ImGui preview | USB debug tunnel ports, companion running | Verify raw stream with ffplay first |
| Garbled decode | missing SPS/PPS, wrong codec | Log first NAL type bytes |
| Stereo depth wrong later | calibration not run | Finish M6 before trusting M17 |

#### Milestones

**M4 — USB stream (single eye, H.264, CPU decode)**

```text
companion: capture PCA → MediaCodec H.264 → TCP send
PC: UsbTcpTransport.recv → packet buffer
    CpuFfmpegDecoder → cv::Mat BGR
    ImGui.showTexture(bgr)
    log StageTimestamp per stage
```

| Acceptance | ≥15 fps 60 s; ImGui live view; latency trace logged |

**M5 — Stereo + metadata**

```text
two TcpTransports (left/right ports)
MetadataReceiver on side port → parse JSON {timestampNs, intrinsics, pose, codec}
pair frames where |t_left - t_right| ≤ 33ms
HealthMonitor.record_sync_offset(delta)
```

| Acceptance | Both eyes ≥15 fps; metadata every frame; sync ≤33 ms |

**M6 — Stereo calibration**

```text
on keypress: save left/right PNG pairs (checkerboard visible)
offline: cv2.stereoCalibrate(CALIB_FIX_INTRINSIC) → stereo_calib.json
startup: load JSON → initUndistortRectifyMap → GPU upload maps
```

| Acceptance | ≥20 pairs; valid JSON; rectified overlay aligned in ImGui |

**M7 — HEVC upgrade (USB Tier 2)**

```text
companion fork: MediaCodec video/hevc + KEY_LATENCY=0
PC: if metadata.codec=="hevc": hevc_cuvid else h264
compare bandwidth and encode latency vs M4 baseline
```

| Acceptance | Stable decode ≥15 fps; bandwidth drop vs H.264; metadata codec correct |

**M8 — NVDEC zero-copy**

```text
NvdecDecoder: AV_PIX_FMT_CUDA NV12 → cv::cuda::cvtColor → BGR GpuMat
no av_hwframe_transfer to host
optional: cudaStream for decode/infer overlap (see §9)
```

| Acceptance | Decode <2 ms avg; zero host memcpy before inference |

**M9 — Wi-Fi WebRTC (Tier 3)**

```text
WifiWebRtcTransport: libdatachannel PeerConnection
SDP: H.264 CBP profile-level-id=42e01f
disable jitter buffer (config TBD — open question)
same downstream pipeline as USB
```

| Acceptance | ≥15 fps; p95 ≤ 2× p50 vs USB; detector match ±1 frame |

---

### Phase 3 — Development Infrastructure

**Goal:** Record sessions; replay without headset; tune via config.

#### Learning objectives

- Separate transport from processing via `IVideoTransport`
- Design regression datasets
- Load YAML config at startup

#### Prerequisites

- File I/O, JSON parsing
- Basic understanding of why replay beats live-only debugging

#### If you are stuck

| Symptom | Check | Next step |
|---------|-------|-----------|
| Replay desync | timestamp monotonicity | Align metadata index to video frames |
| Config ignored | path, parse errors | Log loaded YAML keys at startup |

#### Milestones

**M10 — Record and replay**

```text
VideoRecorder.write(packet) + sidecar index
ReplaySession.readFrame(): read next indexed packet → decode → return Frame
swap live transport for ReplaySession in main — rest unchanged
```

| Acceptance | Record 60 s; replay drives decode without headset |

**M11 — Config and health**

```text
load config.yaml → struct AppConfig
HealthMonitor: aggregate metrics every 1s → ImGui panel
hot-reload selected keys in debug build
```

| Acceptance | All tunables from YAML; panel shows drops + E2E latency |

**M12 — Benchmark runner**

```text
for each dataset in datasets/:
    ReplaySession.run(full_pipeline)
    export CSV(latency, detection_count, filter_residual)
compare to baseline CSV
```

| Acceptance | Batch run completes; CSV diff vs baseline |

---

### Phase 4 — Perception Core

**Goal:** Find kit, segment pads, accelerate inference, stabilise detections, 3D points.

#### Learning objectives

- Classical ROI vs learned detector
- Train/export YOLO segmentation
- ONNX vs TensorRT tradeoffs
- 2D → 3D via stereo triangulation

#### Prerequisites

- CVAT labelling workflow
- Ultralytics YOLO11 docs (train + export)
- Basic linear algebra for triangulation

#### If you are stuck

| Symptom | Check | Next step |
|---------|-------|-----------|
| Low mAP | labels, class imbalance | More kick/snare examples |
| ONNX OK, TRT wrong | letterbox, input size | Fixed `imgsz`; verify with trtexec |
| Jittery 3D | calibration, sync | Re-run M6; check sync offset |

#### Milestones

**M13 — Kit detection + ROI**

```text
IKitDetector.detect(frame) → roi_rect  // contours first
crop frame to roi before IDetector
measure pixel reduction ≥30%
```

| Acceptance | ROI covers all pads on live + replay |

**M14 — Pad segmentation (ONNX)**

```text
label pads in CVAT → train yolo11n-seg → export ONNX
YoloOnnxDetector: letterbox → session.Run → NMS → masks
mask centroid → PadDetection{pad_id, u, v, confidence}
```

| Acceptance | ≥90% class accuracy on held-out replay; all pads visible live |

**M15 — TensorRT**

```text
export engine half=True → TensorRtDetector enqueueV3
compare accuracy vs ONNX on replay datasets
INT8 only if FP16 insufficient
```

| Acceptance | FP16 <8 ms; accuracy within 1% of ONNX |

**M16 — Tracking**

```text
Tier1: ClassLockTracker — match by class_id + EMA on position
Tier2: ByteTrackTracker — Kalman + Hungarian IoU
only confirmed tracks → estimation
```

| Acceptance | No ID swap >1 frame over 5 min replay |

**M17 — 3D localisation**

```text
mono bootstrap: ray from pixel + head pose → plane intersection
stereo target: remap → StereoBM/SGM → sample disparity at centroid
reprojectImageTo3D → WorldFrame via TransformTree
```

| Acceptance | Stereo depth error <5 cm on known pad spacing |

---

### Phase 5 — Pose Estimation and MR Visualization

**Goal:** Smooth poses, render glows — **Perception MVP (M22)**.

#### Learning objectives

- Kalman predict/update intuition
- Interpolate low-rate poses to display refresh
- Passthrough + projection layers in OpenXR

#### Prerequisites

- EKF basics (state, process noise, measurement noise)
- `xrTimeToDisplayTime` in frame loop

#### If you are stuck

| Symptom | Check | Next step |
|---------|-------|-----------|
| Glow drift | interpolation time base | Use predicted display time not capture time |
| Glow wrong color | color space extension | Try `XR_FB_color_space` values |

#### Milestones

**M18 — Pose filtering**

```text
for each pad:
    filter.predict(dt)
    filter.update(TrackedPad measurement)
    PadPoseStore[pad_id] = filter.getPose()
config selects Fixed | EKF | UKF
```

| Acceptance | 14 stable poses live; EKF vs Fixed logged on replay |

**M19 — Replay filter tuning**

```text
BenchmarkRunner.run(replay, filter=ekf, Q/R sweep)
pick Q/R minimising innovation variance without lag
write best values to config.yaml
no headset required
```

| Acceptance | Documented tuning procedure; config updated from replay |

**M20 — MR glow overlays**

```text
render loop:
    t_display = xrTimeToDisplayTime(frameState)
    pose = interpolate(PadPoseStore, t_display)
    for each pad: draw glow quad at pose (world space)
layers: PassthroughFB + Projection
```

| Acceptance | Glow within 2 cm visual error; 72+ Hz interpolated |

**M21 — Robustness**

```text
if frame_variance < threshold: enable CLAHE
on detection dropout: filter.predict-only until measurement returns
```

| Acceptance | Recovers <2 s after 5 s occlusion; dim-room replay passes |

**M22 — Perception MVP gate**

```text
integrate M4–M21 in one binary
run benchmark suite + 10 min live session
checklist §1 Perception MVP
```

| Acceptance | All above; no crash 10 min; benchmarks pass |

---

### Phase 6 — Music and Timing Engine

**Goal:** Parse charts; schedule notes in musical time.

#### Learning objectives

- SMF `.mid` structure
- Beat position vs wall-clock time
- Lookahead scheduling

#### Prerequisites

- MIDI file format overview
- Floating-point beat math

#### Milestones

**M23 — Chart parser**

```text
parse .mid → tracks → NoteOn events on drum channel
map note → pad_id via kit YAML
build TempoMap from meta events
output Song{metadata, chart[], tempo_map}
```

| Acceptance | 5 test files parse; note counts match reference |

**M24 — Tempo engine + scheduler**

```text
beat_clock.advance(now_ns) → current_beat
for note in chart where note.beat in (now, now+lookahead]:
    push CueEvent{pad_id, hit_time_ns} to render queue
```

| Acceptance | Cues fire at correct times on known BPM track |

**M25 — Playback state machine**

```text
states: IDLE → LOADING → COUNT_IN → PLAYING → OUTRO → RESULTS
transitions on user input + chart end
COUNT_IN: metronome only (M34 hooks here)
```

| Acceptance | Full lifecycle without crash |

---

### Phase 7 — Core Gameplay

**Goal:** MIDI hits meet chart notes → score.

#### Milestones

**M26 — MIDI merge**

```text
RtMidi callback → lock-free HitEventQueue (SPSC)
gameplay thread drains queue each frame
```

| Acceptance | Hits logged with timestamp during active XR session |

**M27 — Cue-driven glows**

```text
CueEvent → approach animation (scale/opacity ramp)
glow peaks at hit_time_ns
```

| Acceptance | Cues on correct pads in sync with chart |

**M28 — Hit judgment**

```text
for each HitEvent:
    find nearest NoteEvent within judgment window
    assign Perfect/Great/Good/Miss
    Scorer.update(combo, points)
```

| Acceptance | Judgments within ±2 ms of window edges on test harness |

**M29 — Feedback**

```text
on judgment: flash pad color + play sample (miniaudio)
measure strike-to-flash ≤30 ms
```

| Acceptance | Visible flash + sound; latency budget met |

---

### Phase 8 — User Interface

**Goal:** In-VR menus replace debug-only ImGui for players.

#### Milestones

**M30 — VR menu** — world-space panels; hand or gaze select  
**M31 — Song browser** — scan `songs/`; select → load chart  
**M32 — HUD + results** — score, combo, accuracy breakdown  
**M33 — Calibration wizard** — guide user through M6-like flow in-app  

| Phase acceptance | Navigate menu → pick song → play → see results without desktop UI |

---

### Phase 9 — Audio and Performance Hardening

**M34 — Metronome + hit sounds** (miniaudio)  
**M35 — Backing track** synced to chart  
**M36 — Performance audit** — NSight profile; glass-to-glow ≤90 ms; zero dropped render frames 10 min  

Introduce threading patterns incrementally from Phase 2 onward; M36 is final verification ([§9](#9-performance-and-concurrency)).

---

### Phase 10 — Content and Product Completion

**M37 — `.mid` import UX**  
**M38 — Song library management**  
**M39 — User statistics persistence**  
**M40 — End-to-end product test** — calibrate → import → play → score → review; 30 min stable  

| Product MVP | M40 acceptance = full §1 Product MVP checklist |

---

## 9. Performance and Concurrency

Hard constraints: render 72–120 Hz without drops; MIDI-to-flash ≤30 ms (M29); glass-to-glow ≤90 ms (M36).

### Threads

| Thread | Role | Priority |
|--------|------|----------|
| `xr_thread` | OpenXR + D3D11 render | REALTIME |
| `stream_thread` | Recv + decode | HIGH |
| `cv_thread` | CV pipeline | HIGH |
| `estimation_thread` | `IPoseFilter` | NORMAL |
| `music_thread` | Scheduler | NORMAL |
| `midi_thread` | RtMidi callback | HIGH |
| `audio_thread` | miniaudio mix | HIGH |

Never share a core between `xr_thread` and `cv_thread`. Pin with platform affinity API.

### Lock-free SPSC queues

```text
stream →[FrameRingBuffer]→ cv →[DetectionQueue]→ estimation
estimation →[PadPoseStore double-buffer]→ xr
midi →[HitEventQueue]→ music →[CueEventQueue]→ xr
```

Latest-frame-wins on video ring. Pre-allocate `GpuMat` slots — no alloc in hot loop.

### Zero-copy GPU path

```text
NVDEC → NV12 (GPU) → cvtColor → BGR GpuMat → (CLAHE) → ROI → letterbox → TRT
```

No host round-trip between decode and infer ([§11](#11-implementation-reference)).

### CUDA stream overlap

Decode frame N+1 on stream A while inferring frame N on stream B; sync with `cudaEvent`.

### Pose interpolation

Render uses two pose snapshots + `xrTimeToDisplayTime` for smooth 90 Hz glows from 10–30 Hz CV updates.

---

## 10. Latency and Bandwidth Budget

Single authoritative table. All times from unified clock (§4).

### Perception (USB, Tier 2)

| Stage | Tier 1 | Tier 2 | Notes |
|-------|--------|--------|-------|
| Capture | ~33 ms | ~33 ms | ~30 fps |
| Encode | ~5–15 ms | ~5–8 ms | HEVC + low-latency keys |
| Transport | ~1–5 ms | ~1–3 ms | TCP_NODELAY |
| Decode | 5–8 ms CPU | <2 ms | NVDEC; stream overlap |
| NV12→BGR | 1–3 ms | <0.5 ms | GPU |
| CLAHE | — | <1 ms | Adaptive M21 |
| Infer | 15–30 ms | 3–8 ms | TRT + ROI crop |
| Track + filter | <2 ms | <2 ms | |
| **CV subtotal** | ~60–100 ms | ~45–55 ms | Infer path only |
| **Glass-to-glow** | — | ~70–90 ms | + display path |

### Display + MIDI feedback

| Path | Budget |
|------|--------|
| Render + link | 8–20 ms |
| Strike-to-flash | ≤30 ms worst (M29) |

### Bandwidth

| Stream | Bitrate |
|--------|---------|
| Mono H.264 1280×960 @30 | ~8–15 Mbps |
| Stereo HEVC Tier 2 | ~10–20 Mbps |
| Video to headset | 0 (passthrough compositor) |

---

## 11. Implementation Reference

### 11.1 Video decode

**Tier 1 CPU:**

```cpp
avcodec_send_packet(ctx, &pkt);
avcodec_receive_frame(ctx, frame);
sws_scale(..., bgr_host);
bgr_gpu.upload(bgr_host);
```

**Tier 2 NVDEC:**

```cpp
// hevc_cuvid or h264_cuvid → AV_PIX_FMT_CUDA
cv::cuda::cvtColor(nv12_gpu, bgr_gpu, cv::COLOR_YUV2BGR_NV12);
```

### 11.2 Inference binding

**ORT (middle step):**

```cpp
Ort::IoBinding binding(session);
binding.BindInput("images", Ort::Value::CreateTensor(cuda_mem, bgr_gpu.ptr(), ...));
session.Run({}, binding);
```

**TensorRT (target):**

```cpp
context->setTensorAddress("images", bgr_gpu.ptr<void>());
context->enqueueV3(stream);
```

### 11.3 Metadata side channel

```json
{
  "timestampNs": 0,
  "camera": "left",
  "codec": "h264",
  "intrinsics": { "fx": 0, "fy": 0, "cx": 0, "cy": 0 },
  "cameraPose": { "position": [0,0,0], "orientation": [0,0,0,1] }
}
```

### 11.4 Stereo triangulation

```cpp
cv::cuda::remap(left, rect_l, map1_l, map2_l, cv::INTER_LINEAR);
stereo->compute(rect_l, rect_r, disparity);
cv::cuda::reprojectImageTo3D(disparity, points3d, Q);
// sample points3d at mask centroid
```

### 11.5 OpenXR frame loop

```cpp
xrWaitFrame(&frameState);
xrBeginFrame(session);
xrLocateSpace(viewSpace, stageSpace, &head_pose);
// interpolate PadPose to frameState.predictedDisplayTime
build_glow_quads(pad_poses);
xrEndFrame(session, {passthrough_layer, projection_layer});
```

---

## 12. Glossary, AI Tutor Protocol, and Document History

### Glossary

| Term | Definition |
|------|------------|
| Phase | Roadmap stage (1–10) |
| Milestone | Deliverable M1–M40 |
| Tier | Technology ceiling 1–4 |
| PCA | Platform passthrough camera API |
| IVideoTransport | Abstract frame source |
| IPoseFilter | Pose smoother interface |
| Glass-to-glow | Camera photon → glow visible |
| Strike-to-flash | Stick hit → visual feedback |
| Latest-frame-wins | Drop stale frames in ring buffer |
| CBP | H.264 Constrained Baseline (Wi-Fi path) |

### AI Tutor Protocol

When helping on VRDrummer:

1. Read user's **Project State** (below)
2. Map to phase + milestone via §8 acceptance criteria
3. Check prerequisites for that phase
4. Give **one next step**, verification command, and pseudocode — not full solution unless asked
5. Prefer replay/debug paths (M10+) over live headset when diagnosing CV

**Project State Template** (paste into chat):

```text
Current milestone: M__
Last passing acceptance test: __
Artefact / branch: __
Blocker: __
```

**Milestone quick index:**

| Range | Phase |
|-------|-------|
| M1–M3 | Platform |
| M4–M9 | Streaming |
| M10–M12 | Infrastructure |
| M13–M17 | Perception |
| M18–M22 | Pose + Perception MVP |
| M23–M25 | Music engine |
| M26–M29 | Gameplay |
| M30–M33 | UI |
| M34–M36 | Audio + perf |
| M37–M40 | Product completion |

### Learning index (by topic)

| Topic | Phase | Milestones |
|-------|-------|------------|
| MIDI / C++ callbacks | 1 | M1 |
| OpenXR | 1 | M2–M3 |
| Video / codecs | 2 | M4–M9 |
| Replay / config | 3 | M10–M12 |
| CV / ML | 4 | M13–M17 |
| Filters / MR render | 5 | M18–M22 |
| Charts / timing | 6–7 | M23–M29 |
| VR UI | 8 | M30–M33 |

### Document History

| Version | Date | Summary |
|---------|------|---------|
| **1.0** | **June 2026** | Initial consolidated design: hardware-agnostic architecture, M1–M40 roadmap with per-phase learning blocks, HEVC Tier 2 USB strategy, interface abstractions, AI tutor protocol; reference hardware externalized to HARDWARE.md |
