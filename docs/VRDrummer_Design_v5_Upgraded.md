# VRDrummer — Design Document

**Version:** 4.1
**Last updated:** June 2026
**Repository:** [github.com/AbhijeetPatil5/VRDrummer](https://github.com/AbhijeetPatil5/VRDrummer)
**Status:** Authoritative architecture and implementation reference

---

## Table of Contents

1. [Vision and Scope](#1-vision-and-scope)
2. [Architecture](#2-architecture)
3. [Transport Pathways: USB and Wi-Fi](#3-transport-pathways-usb-and-wi-fi)
4. [Technology Stack](#4-technology-stack)
5. [Module Specifications](#5-module-specifications)
6. [Phased Adoption Tiers](#6-phased-adoption-tiers)
7. [Build Roadmap](#7-build-roadmap)
8. [Latency and Bandwidth Budget](#8-latency-and-bandwidth-budget)
8.1. [Additional Performance Optimizations](#81-additional-performance-optimizations)
9. [Pedagogy and Codebase](#9-pedagogy-and-codebase)
10. [Open Questions](#10-open-questions)
11. [Glossary](#11-glossary)

---

## 1. Vision and Scope

### Vision

VRDrummer teaches novice drummers using mixed reality. The player wears a Meta Quest 3S,
sees their real Simmons Titan 50 B-EX through passthrough, and receives circular glow cues
on each physical pad. Strikes are captured via real drumsticks (USB MIDI — not VR
controllers). Timing feedback and song charts come **last**, after perception works reliably.

### Build Priority

Phase labels match [§7 Build Roadmap](#7-build-roadmap) milestone IDs.


| Phase    | Deliverable                                                                |
| -------- | -------------------------------------------------------------------------- |
| **0**    | MIDI on PC ✅                                                               |
| **1a**   | USB camera stream (bootstrap: H.264, CPU decode)                           |
| **1b**   | NVDEC zero-copy decode on PC                                               |
| **1c**   | Stereo dual-port + metadata + **stereo calibration harness**               |
| **1d**   | Wi-Fi WebRTC stream (after USB path proven)                                |
| **2–7**  | Perception MVP: kit detect → pad ID → EKF → MR glows → robustness → replay |
| **8–12** | Gameplay **(last):** MIDI merge, charts, scoring, `.mid` / `.pdf`          |


### Perception MVP (First "It Works" Moment)

- Live passthrough camera frames decoded on PC (RTX 4080 Super)
- Drum kit localised in frame; 14 Simmons pads identified
- Stable pad poses via EKF
- Glow circles composited in headset over real pads via OpenXR passthrough layers
- **No chart or timing logic required**

### Hardware


| Component         | Model                                          |
| ----------------- | ---------------------------------------------- |
| Headset           | Meta Quest 3S, Horizon OS v74+, developer mode |
| Drum kit          | Simmons Titan 50 B-EX (USB MIDI)               |
| PC                | AMD 7800X3D, RTX 4080 Super, Windows 11        |
| Link (MR display) | Wired USB-C ≥ 2 Gbps                           |


---

## 2. Architecture

### Foundation Principle

Five stages where latency accumulates:

```text
[1 Capture]        PCA on Quest (~30 fps, ~40–60 ms glass-to-texture)
[2 Encode]         MediaCodec HW on Snapdragon XR2 Gen 2 (H.264 → HEVC Tier 2)
[3 Transport]      Quest → PC: USB adb TCP (Path A) or Wi-Fi WebRTC (Path B)
[4 Decode+Pre]     NVDEC on RTX 4080 Super → cv::cuda color convert → zero-copy
[5 Infer+Estimate] TensorRT YOLO11-seg → ByteTrack → stereo triangulation → EKF
[6 Display]        OpenXR passthrough + glow projection layers → Link → headset
```

**Poses never leave PC RAM.** Only composited MR frames travel back to the headset.
No video is sent back for display.

### Dual Pipeline (Critical — Do Not Conflate)


| Pipeline       | Direction   | Payload                                               |
| -------------- | ----------- | ----------------------------------------------------- |
| **CV video**   | Quest → PC  | Compressed PCA frames (~8–20 Mbps)                    |
| **MR display** | PC → Quest  | Passthrough compositor + glow geometry via Link       |
| **Pad poses**  | PC internal | 14 × `{position, normal, radius}` — **not networked** |


### System Diagram (Target State)

```mermaid
flowchart TB
    subgraph quest [Quest3S]
        PCA_L[PCA_left_1280x960_or_1280x1280]
        PCA_R[PCA_right]
        MC[MediaCodec_HEVC_HW_encoder]
        Meta[Metadata_intrinsics_pose_JSON]
        Transport[USB_adb_TCP_or_WiFi_WebRTC]
    end

    subgraph pc [PC_VRDrummer]
        Decode[NVDEC_FFmpeg_CUDA_zero_copy]
        ColorConv[cv_cuda_NV12_to_BGR]
        CLAHE[cv_cuda_CLAHE_contrast_norm]
        Infer[TensorRT_YOLO11n_seg_INT8]
        Tracker[ByteTrack_Hungarian_IoU]
        Stereo[StereoRectify_Triangulate_or_MonoRay]
        EKF[EKF_pad_pose_filter]
        Poses[PadPoseStore_14_pads]
        XR[OpenXR_D3D11_XR_FB_color_space]
        PT[XR_FB_passthrough]
        Glow[Projection_glow_layers]
        HT[XR_EXT_hand_tracking_M9_plus]
    end

    subgraph out [Headset_view]
        LinkMR[Link_composited_MR]
    end

    PCA_L --> MC
    PCA_R --> MC
    MC --> Transport
    Meta --> Transport
    Transport --> Decode
    Decode --> ColorConv --> CLAHE --> Infer
    Infer --> Tracker --> Stereo --> EKF --> Poses
    HT -.->|M9_plus_only| EKF
    Poses --> Glow
    PT --> XR
    Glow --> XR
    XR --> LinkMR
```



### 3D Localisation Strategy


| Tier           | Method                                                                                                                                                                                                                        | When                  |
| -------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------- |
| **Bootstrap**  | Mono PCA + intrinsics + OpenXR head pose → ray cast                                                                                                                                                                           | M2–M3                 |
| **Target**     | Stereo PCA (both eyes) → rectify → triangulate                                                                                                                                                                                | M3+ after dual stream |
| **References** | [QuestStereoMatching](https://github.com/t-34400/QuestStereoMatching), [CameraToWorld sample](https://github.com/oculus-samples/Unity-PassthroughCameraApiSamples/tree/main/Assets/PassthroughCameraApiSamples/CameraToWorld) | —                     |


Use PCA-provided **intrinsics and per-camera pose** from
[MRUK `PassthroughCameraAccess`](https://developers.meta.com/horizon/documentation/unity/unity-pca-documentation/)
— do **not** hardcode baseline or focal length.

### OpenXR Display (PC via Link)


| Step          | API                                                                                     | Doc                                                                                                                     |
| ------------- | --------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| Extensions    | `XR_FB_passthrough`, `XR_KHR_D3D11_enable`, `XR_FB_color_space`, `XR_EXT_hand_tracking` | [Meta passthrough](https://developers.meta.com/horizon/documentation/native/android/mobile-passthrough/)                |
| D3D11 bind    | `xrGetD3D11GraphicsRequirementsKHR` → `XrGraphicsBindingD3D11KHR`                       | [Khronos D3D11](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrGraphicsBindingD3D11KHR.html)                  |
| Color space   | `xrSetColorSpaceFB(XR_COLOR_SPACE_RIFT_CV1_FB)` after session create                    | [XR_FB_color_space](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_FB_color_space.html)                      |
| Layers        | `XrCompositionLayerPassthroughFB` + `XrCompositionLayerProjection`                      | [XrPassthrough sample](https://github.com/meta-quest/Meta-OpenXR-SDK/tree/main/Samples/XrSamples/XrPassthrough)         |
| Hand tracking | `xrLocateHandJointsEXT` → wrist pose for EKF prior                                      | [XR_EXT_hand_tracking](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_EXT_hand_tracking.html)                |
| Link setup    | Developer Runtime + Passthrough over Link beta                                          | [Passthrough over Link](https://developers.meta.com/horizon/documentation/native/android/mobile-passthrough-over-link/) |


---

## 3. Transport Pathways: USB and Wi-Fi

Build **USB first (Path A)**. Wi-Fi **(Path B)** only after Path A acceptance tests pass.

### Path A — USB (Primary)

**Use when:** Daily development; lowest jitter; Link + PCA simultaneously.


| Layer            | Bootstrap (Tier 1)          | Target (Tier 2)                                                                  | Package / command                                                                                                        |
| ---------------- | --------------------------- | -------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| Capture          | PCA foreground service      | Same                                                                             | [camera-fgs](https://github.com/t-34400/camera-fgs) APK                                                                  |
| Encode           | `MediaCodec` H.264 HW       | **HEVC HW** fork                                                                 | Android [MediaCodec](https://developer.android.com/reference/android/media/MediaCodec)                                   |
| Encoder tuning   | Default                     | `KEY_LATENCY=0`, `KEY_PREPEND_HEADER_TO_SYNC_FRAMES=1`, `KEY_I_FRAME_INTERVAL=1` | [MediaFormat keys](https://developer.android.com/reference/android/media/MediaFormat)                                    |
| Transport        | TCP over `adb forward`      | `adb reverse` or same                                                            | `adb forward tcp:18081 tcp:8081` (left), `18082→8082` (right)                                                            |
| Metadata         | TCP port `18090` (optional) | Same                                                                             | JSON intrinsics/pose; never blocks video stream                                                                          |
| Future transport | —                           | UDP/RTP (Tier 3 fork)                                                            | [libdatachannel RTP](https://github.com/paullouisageneau/libdatachannel)                                                 |
| Future transport | —                           | AoA direct bulk (research)                                                       | Android `UsbAccessory`, PC `WinUSB` / `libusb`                                                                           |
| PC receive       | `TcpH264Receiver`           | Same                                                                             | BSD sockets                                                                                                              |
| Decode           | FFmpeg CPU `libavcodec`     | **FFmpeg NVDEC**                                                                 | [NVIDIA FFmpeg guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/ffmpeg-with-nvidia-gpu/index.html) |
| Verify           | ffplay                      | —                                                                                | [camera-fgs ffplay recipe](https://github.com/t-34400/camera-fgs)                                                        |


**camera-fgs already uses Android `MediaCodec` H.264 hardware encoder.** The Tier 1 → Tier 2
upgrade is **HEVC + latency key tuning**, not software → hardware.

**Link coexistence:** camera-fgs exists specifically because
[PCA is not supported over Link](https://developers.meta.com/horizon/documentation/native/android/pca-native-overview/)
as an app-facing API.

#### Tier 2 USB Upgrades (after M1a baseline)


| Upgrade            | Action                                                    |
| ------------------ | --------------------------------------------------------- |
| Stereo             | Forward both ports; sync timestamps via metadata JSON     |
| HEVC               | Fork camera-fgs: `video/hevc`; PC: `hevc_cuvid` / NVDEC   |
| Low-latency encode | `KEY_LATENCY=0`, short GOP, prepend SPS/PPS               |
| UDP/RTP            | Replace raw TCP in companion fork; PC: libdatachannel RTP |
| AoA (research)     | Direct bulk USB transfer; bypasses adb daemon entirely    |


#### PCA Resolution Options (Horizon OS v83+)


| Resolution      | Aspect | Use case                                         |
| --------------- | ------ | ------------------------------------------------ |
| 1280 × 960      | 4:3    | Standard; wide horizontal coverage               |
| **1280 × 1280** | 1:1    | Taller FOV; better coverage of kick / floor toms |


Probe `CameraInfo` at runtime — do not hardcode. Test both resolutions in M1a
and record which covers all 14 pads from your seated position. Handle aspect ratio
change in YOLO letterboxing.

### Path B — Wi-Fi (Secondary)

**Use when:** USB path proven; want untethered camera. Keep wired Link for MR until
separately validated.


| Layer      | Implementation                                                                | Package                                                                                                                                      |
| ---------- | ----------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| Capture    | PCA via Unity companion                                                       | [QuestCameraKit](https://github.com/xrdevrob/QuestCameraKit) `WebRTC-Quest`                                                                  |
| Signaling  | WebSocket on LAN                                                              | [NativeWebSocket](https://github.com/endel/NativeWebSocket) + [setup video](https://www.youtube.com/watch?v=-CwJTgt_Z3M)                     |
| Transport  | WebRTC SRTP/RTP                                                               | [Unity WebRTC](https://github.com/Unity-Technologies/com.unity.webrtc), [SimpleWebRTC](https://github.com/FireDragonGameStudio/SimpleWebRTC) |
| PC receive | `libdatachannel` `PeerConnection` + `H264RtpDepacketizer`                     | [media-receiver example](https://github.com/paullouisageneau/libdatachannel/blob/master/examples/media-receiver/main.cpp)                    |
| Alt        | [QuestVisionStream](https://github.com/danieloquelis/Unity-QuestVisionStream) | PCA → WebRTC for external GPU                                                                                                                |
| Decode     | Same FFmpeg / NVDEC stack as USB                                              | —                                                                                                                                            |


**Wi-Fi tuning (mandatory for usable CV latency):**

- Quest + PC on the **same 5 GHz or 6 GHz AP**; prefer Wi-Fi 6E (6 GHz) if available
- SDP offer must negotiate **H.264 Constrained Baseline Profile** (`profile-level-id=42e01f`)
— prevents decoder from waiting for B-frames
- PC-side `libdatachannel` receiver: **disable jitter buffer** (`googNoDelay=true` equivalent;
implementation-specific in libdatachannel `rtc::Configuration`)
- Without these two settings, default WebRTC adds 200–400 ms of playout delay,
making real-time CV unusable

**Path selection in code:** `IVideoTransport` interface — `UsbTcpTransport` | `WifiWebRtcTransport`.

---

## 4. Technology Stack

### `IVideoTransport` Interface Contract

All transport implementations must satisfy this interface:

```cpp
struct Frame {
    cv::cuda::GpuMat  gpu_bgr;       // decoded BGR on device (zero-copy from NVDEC)
    int64_t           timestamp_ns;  // monotonic nanoseconds (Quest-side PCA timestamp)
    CameraId          camera;        // LEFT or RIGHT
};

class IVideoTransport {
public:
    virtual ~IVideoTransport() = default;
    virtual bool  start()  = 0;   // open socket / WebRTC peer; begin receiving
    virtual void  stop()   = 0;   // graceful shutdown
    // Non-blocking: returns false if no new frame since last call (latest-frame-wins)
    virtual bool  readFrame(Frame& out) = 0;
};
```

`UsbTcpTransport` and `WifiWebRtcTransport` both implement this.
The CV pipeline never calls transport-specific code directly.

**USB transport tuning (M1a):** Set `TCP_NODELAY` and a small recv buffer on
`UsbTcpTransport` to reduce Nagling delay on localhost `adb forward`.

### PC Application (VRDrummer.exe)


| Component    | Bootstrap (Tier 1)        | Target (Tier 2–3)                                  | Link                                                                                                                                           |
| ------------ | ------------------------- | -------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| Build        | CMake ≥ 3.20              | Same                                               | [cmake.org](https://cmake.org/download/)                                                                                                       |
| Language     | C++17                     | Same                                               | —                                                                                                                                              |
| OpenXR       | 1.1.59.1                  | Same + `XR_FB_color_space`, `XR_EXT_hand_tracking` | [OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK/releases/tag/release-1.1.59.1)                                                         |
| Graphics     | D3D11 + GLM               | Same                                               | [XR_KHR_D3D11_enable](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_KHR_D3D11_enable.html), [glm](https://github.com/g-truc/glm)   |
| Decode       | FFmpeg CPU `libavcodec`   | FFmpeg **NVDEC** (`h264_cuvid` / `hevc_cuvid`)     | [NVIDIA FFmpeg](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/ffmpeg-with-nvidia-gpu/index.html)                             |
| Preprocess   | OpenCV CPU                | OpenCV **CUDA** (`cv::cuda`) + CLAHE               | [OpenCV](https://github.com/opencv/opencv/releases)                                                                                            |
| Inference    | ONNX Runtime CUDA EP      | **TensorRT** `.engine` (FP16 → INT8)               | [ONNX Runtime](https://github.com/microsoft/onnxruntime/releases), [Ultralytics TensorRT](https://docs.ultralytics.com/integrations/tensorrt/) |
| ORT→TRT step | —                         | ONNX Runtime TensorRT EP (middle step)             | [ORT TRT EP](https://onnxruntime.ai/docs/execution-providers/TensorRT-ExecutionProvider.html)                                                  |
| Model        | YOLO11n-seg ONNX          | **YOLO11n-seg** TensorRT INT8 engine               | [Ultralytics](https://github.com/ultralytics/ultralytics)                                                                                      |
| Tracking     | None                      | **ByteTrack / SORT** (Hungarian + IoU)             | [ByteTrack](https://github.com/ifzhang/ByteTrack)                                                                                              |
| Estimation   | Eigen3 EKF (mono)         | EKF + stereo fusion + hand tracking prior          | [Eigen](https://gitlab.com/libeigen/eigen/-/releases)                                                                                          |
| MIDI         | RtMidi 6.0.0 ✅            | Same                                               | [rtmidi](https://github.com/thestk/rtmidi/releases/tag/6.0.0)                                                                                  |
| Debug        | Dear ImGui, nlohmann/json | Same                                               | [imgui](https://github.com/ocornut/imgui), [json](https://github.com/nlohmann/json)                                                            |
| Labeling     | CVAT                      | Same                                               | [cvat-ai/cvat](https://github.com/cvat-ai/cvat)                                                                                                |


### Quest Companion


| Role             | Bootstrap                             | Target                                                             | Link                                                                               |
| ---------------- | ------------------------------------- | ------------------------------------------------------------------ | ---------------------------------------------------------------------------------- |
| Stream app       | camera-fgs APK (H.264, left eye only) | Fork: HEVC + stereo (both ports) + low-latency keys + optional UDP | [camera-fgs](https://github.com/t-34400/camera-fgs)                                |
| Stereo reference | —                                     | [UXR.QuestCamera](https://uralstech.github.io/UXR.QuestCamera/)    | Dual-eye simultaneous PCA                                                          |
| Wi-Fi alt        | —                                     | QuestCameraKit WebRTC                                              | [QuestCameraKit](https://github.com/xrdevrob/QuestCameraKit)                       |
| PCA reference    | —                                     | MRUK / Meta samples                                                | [PCA samples](https://github.com/oculus-samples/Unity-PassthroughCameraApiSamples) |


### Inference Export Commands (run on RTX 4080 Super)

```bash
# Tier 1 — ONNX (bootstrap)
yolo train model=yolo11n-seg.pt data=drum_pads.yaml
yolo export model=runs/segment/train/weights/best.pt format=onnx

# Tier 2 — TensorRT FP16
yolo export model=best.pt format=engine half=True device=0

# Tier 2 — TensorRT INT8 (requires calibration data)
yolo export model=best.pt format=engine int8=True data=drum_pads.yaml device=0
```

C++ reference: [YOLOv8-ONNXRuntime-CPP](https://github.com/ultralytics/ultralytics/tree/main/examples/YOLOv8-ONNXRuntime-CPP)
(pattern applies to YOLO11 ONNX and TensorRT alike).

---

## 5. Module Specifications

### Repository Layout

```
VRDrummer/
├── companion/
│   ├── camera-fgs-fork/           # HEVC + stereo + UDP (Tier 2)
│   └── QuestCameraKit-scene/      # Wi-Fi WebRTC path (Tier 3)
├── calibration/                   # Stereo extrinsics capture harness (M1c)
│   ├── capture_stereo_pairs.cpp   # saves synced left+right checkerboard frames
│   └── calibrate_stereo.py        # cv2.stereoCalibrate → stereo_calib.json
├── docs/DESIGN.md
├── src/
│   ├── stream/
│   │   ├── IVideoTransport.h          # interface contract
│   │   ├── UsbTcpTransport.cpp        # Path A
│   │   ├── WifiWebRtcTransport.cpp    # Path B
│   │   ├── FfmpegDecoder.cpp          # CPU bootstrap → NVDEC target
│   │   └── FrameRingBuffer.cpp        # latest-frame-wins
│   ├── cv/
│   │   ├── KitDetector.cpp            # coarse ROI
│   │   ├── PadDetectorOnnx.cpp        # Tier 1
│   │   ├── PadDetectorTrt.cpp         # Tier 2
│   │   ├── ByteTracker.cpp            # Hungarian + IoU between YOLO and EKF
│   │   ├── StereoCalib.cpp            # load stereo_calib.json, build rectify maps
│   │   └── StereoTriangulation.cpp    # rectify → disparity → 3D
│   ├── estimation/
│   │   └── ekf_pad_pose.cpp           # state [x,y,z,vx,vy,vz]; fuses vision + hand tracking
│   ├── xr/                            # OpenXR session, passthrough, hand tracking
│   ├── render/                        # D3D11 glow shaders, projection layer
│   ├── midi/                          # RtMidi, Simmons map, hit queue
│   └── timing/                        # (deferred) chart, scoring
├── third_party/                       # FFmpeg, ONNX Runtime, TensorRT (not in git)
├── midiTest.cpp
├── vrTest.cpp
└── CMakeLists.txt
```

### Module: `stream`

**Input:** Compressed H.264/HEVC byte stream from Quest
**Output:** `Frame` (CUDA `GpuMat` BGR + timestamp) in ring buffer

**Policy:** Latest-frame-wins — the ring buffer always holds the most recent decoded frame.
The CV thread never blocks waiting for an older frame.

#### Bootstrap Decode (CPU, Tier 1)

```cpp
// Teaching skeleton — not drop-in production code
AVCodec* dec = avcodec_find_decoder(AV_CODEC_ID_H264);
// avcodec_alloc_context3 → avcodec_open2 → per-packet:
// avcodec_send_packet → avcodec_receive_frame → sws_scale → cv::Mat BGR
```

#### Target Decode — NVDEC Zero-Copy (Tier 2)

Full pipeline: NVDEC decodes → NV12 CUDA surface → `cv::cuda::cvtColor` → BGR
`GpuMat` on GPU. **No data leaves the GPU between decode and inference.**

```cpp
// 1. Create CUDA hardware device context
AVBufferRef* hw_device_ctx = nullptr;
av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_CUDA,
                        nullptr, nullptr, 0);

// 2. Attach to codec context BEFORE avcodec_open2
codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);

// 3. Select hardware decoder explicitly
// For H.264:  avcodec_find_decoder_by_name("h264_cuvid")
// For HEVC:   avcodec_find_decoder_by_name("hevc_cuvid")

// 4. After avcodec_receive_frame:
//    frame->format == AV_PIX_FMT_CUDA  → data lives on GPU as NV12
//    av_hwframe_transfer_data() is NOT called — stay on GPU

// 5. Map to cv::cuda::GpuMat (NV12) and color-convert in-place:
//    cv::cuda::cvtColor(nv12_gpu, bgr_gpu, cv::COLOR_YUV2BGR_NV12)
```

**FFmpeg build requirement:** PC FFmpeg must be compiled with CUDA/NVDEC support
(`--enable-nvdec` / `--enable-cuda`). BtbN Windows builds typically include this.
**OpenCV build requirement:** `cv::cuda::*` requires OpenCV built with CUDA — not the
default `pip install opencv-python` wheel on Windows.

Reference: [NVIDIA Video Codec SDK — NVDEC guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/ffmpeg-with-nvidia-gpu/index.html)

#### Inference Binding After NVDEC (split by tier)

**Tier 1b — ONNX Runtime CUDA EP + IOBinding** (optional middle step before native TRT):

```cpp
// bgr_gpu from NVDEC path above — stays on device
Ort::IoBinding binding(session);
binding.BindInput("images", Ort::Value::CreateTensor(
    memory_info_cuda, bgr_gpu.ptr<uchar>(), ...));
binding.BindOutput("output0", memory_info_cuda);
session.Run(Ort::RunOptions{nullptr}, binding);
```

Reference: [ORT TensorRT EP](https://onnxruntime.ai/docs/execution-providers/TensorRT-ExecutionProvider.html)
as an alternative middle step before hand-rolling TensorRT.

**Tier 2 — TensorRT native bindings** (target; not ORT IOBinding):

```cpp
// Ultralytics-exported .engine; bind input tensor directly to bgr_gpu device pointer
context->setTensorAddress("images", bgr_gpu.ptr<void>());
context->enqueueV3(stream);  // cudaStream_t from decode/infer pipeline
```

Reference: [Ultralytics TensorRT export](https://docs.ultralytics.com/integrations/tensorrt/).
Validate **FP16** (`half=True`) before INT8 calibration.

#### Metadata Side Channel

Send alongside each compressed frame on **metadata TCP port `18090`** (or WebRTC DataChannel):

```json
{
  "timestampNs": 1234567890123,
  "camera": "left",
  "intrinsics": {
    "fx": 0.0, "fy": 0.0, "cx": 0.0, "cy": 0.0,
    "distortion": [0.0, 0.0, 0.0, 0.0, 0.0]
  },
  "cameraPose": {
    "position": [0.0, 0.0, 0.0],
    "orientation": [0.0, 0.0, 0.0, 1.0]
  }
}
```

Source on Quest: MRUK `PassthroughCameraAccess.Intrinsics`, `GetCameraPose()`.
Stereo pair timestamps must be within **1 frame period (≤ 33 ms)** — log offset in M1c.

---

### Module: `cv`

**Input:** `Frame` (CUDA `GpuMat` BGR) from `stream`
**Output:** `std::vector<PadDetection>` per frame

#### Pipeline Stages


| Stage                   | Tier 1                             | Tier 2                                            | Tool                                                                                                               |
| ----------------------- | ---------------------------------- | ------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| Contrast normalisation  | None                               | **CLAHE** (robustness, low light)                 | `cv::cuda::createCLAHE`                                                                                            |
| Kit ROI                 | OpenCV contours / colour threshold | YOLO class `drum_kit`; **crop before seg** (§8.1) | OpenCV, Ultralytics                                                                                                |
| Pad detection           | YOLO11n-seg ONNX                   | **YOLO11n-seg**, TensorRT INT8                    | [ONNX Runtime C++](https://onnxruntime.ai/docs/api/c/), [TRT](https://docs.ultralytics.com/integrations/tensorrt/) |
| Centroid                | Bounding box centre                | **Mask centroid** (sub-pixel accurate)            | Segmentation output                                                                                                |
| Detection stabilisation | None                               | **ByteTrack / SORT** (Hungarian + IoU)            | [ByteTrack](https://github.com/ifzhang/ByteTrack)                                                                  |
| 3D localisation         | Mono ray + head pose               | **Stereo triangulation**                          | See below                                                                                                          |


#### CLAHE (Low-Light Robustness)

```cpp
// Apply before YOLO input; runs fully on GPU — < 1 ms on 4080 Super
cv::Ptr<cv::cuda::CLAHE> clahe = cv::cuda::createCLAHE(/*clipLimit=*/2.0,
                                                         cv::Size(8,8));
clahe->apply(bgr_gpu, bgr_gpu_eq);
```

Addresses M6 dim/bright-light failure mode directly.

#### Detection Tracking (ByteTrack / SORT)

Sits **between** the YOLO output and the EKF input. Prevents spurious detections from
entering the filter:

1. Each YOLO frame produces `N` bounding boxes / mask centroids
2. Hungarian algorithm matches detections to existing tracks via IoU
3. Unmatched detections for ≥ 3 consecutive frames are discarded (spurious)
4. Matched tracks produce stable `track_id → pad_class` assignments
5. Only confirmed tracks are sent to the EKF as `PadDetection`

Reference C++ implementations: [SORT-cpp](https://github.com/yasenh/sort-cpp),
[ByteTrack](https://github.com/ifzhang/ByteTrack).

#### Stereo Triangulation Pipeline

**Prerequisites:** Stereo calibration JSON (`stereo_calib.json`) produced by the
calibration harness in **M1c** (see §5 Calibration Harness).

```
Left frame (rectified) ──► StereoRectify ──┐
                                            ├──► StereoBM / StereoSGBM ──► Disparity ──► 3D point
Right frame (rectified) ──► StereoRectify ──┘                               cv::reprojectImageTo3D
```

```cpp
// Load rectification maps once at startup (from stereo_calib.json)
cv::initUndistortRectifyMap(K_left,  D_left,  R1, P1, imgSize,
                             CV_32F, map1_left,  map2_left);
cv::initUndistortRectifyMap(K_right, D_right, R2, P2, imgSize,
                             CV_32F, map1_right, map2_right);

// Per stereo frame pair:
cv::cuda::remap(left_gpu,  rect_left,  map1_left_gpu,  map2_left_gpu,  cv::INTER_LINEAR);
cv::cuda::remap(right_gpu, rect_right, map1_right_gpu, map2_right_gpu, cv::INTER_LINEAR);

// GPU stereo matching
cv::Ptr<cv::cuda::StereoBM> sgbm = cv::cuda::createStereoBM(/*nDisp=*/64);
sgbm->compute(rect_left, rect_right, disparity_gpu);

// Reproject to 3D using Q matrix from stereoRectify
cv::cuda::reprojectImageTo3D(disparity_gpu, points3D_gpu, Q_gpu);
// Sample points3D at pad centroid pixel coords → world-space pad position
```

**Do not hardcode the stereo baseline.** Read `t` (translation vector) from
`stereo_calib.json` produced by `cv::stereoCalibrate`. The Quest 3S PCA provides
per-camera intrinsics but **not** stereo extrinsics via API.

---

### Module: `estimation`

**Input:** Confirmed `PadDetection` from ByteTrack + OpenXR head pose +
(Tier 2) OpenXR wrist joints
**Output:** Filtered `PadPose[14]` in world space

#### EKF State

Per-pad state vector: \[x, y, z, v_x, v_y, v_z\]

- **Predict:** constant-velocity model with process noise tuned to expected
pad movement (none during playing; occasional large jump when kit moved)
- **Update (vision):** 3D position from stereo triangulation or mono ray,
weighted by detection confidence from YOLO
- **Update (hand tracking):** Wrist pose from `xrLocateHandJointsEXT` constrains
which pad is about to be struck — provides a soft prior to the EKF measurement
noise for that pad during the predictive-glow phase (Milestone 9+)

#### OpenXR Fusion APIs


| API                     | Purpose                                      |
| ----------------------- | -------------------------------------------- |
| `xrLocateSpace`         | Head / view space in world frame each frame  |
| `xrLocateViews`         | Per-eye view matrices for ray projection     |
| `xrLocateHandJointsEXT` | Wrist joint pose for strike prediction prior |


**Interpolation:** Pad poses update at CV rate (~10–30 Hz). The render thread runs
at 90 Hz. Interpolate `PadPose[14]` to `xrTimeToDisplayTime` (predicted display time
from `XrFrameState`) — not raw frame capture timestamp — to reduce visual lag.
Use linear interpolation between CV updates via the atomic snapshot to prevent glow judder.

---

### Module: `xr` + `render`

**Input:** `PadPose[14]` from `estimation`
**Output:** Composited MR frame to Quest via Link

#### OpenXR Session Setup (all extensions)

```cpp
const char* extensions[] = {
    "XR_FB_passthrough",
    "XR_KHR_D3D11_enable",
    "XR_FB_color_space",          // match projection layer to headset colour space
    "XR_EXT_hand_tracking"        // wrist joints for EKF prior
};
// xrCreateInstance with above extensions
// xrSetColorSpaceFB(XR_COLOR_SPACE_RIFT_CV1_FB) — prevents hue shift between
// passthrough background and rendered glow quads
```

#### Frame Loop

```
xrWaitFrame → xrBeginFrame
  xrLocateSpace (world)
  xrLocateViews (per-eye)
  xrLocateHandJointsEXT (both hands)          // M9+ only; wrist prior into EKF
  → interpolate PadPose to xrTimeToDisplayTime
  → build glow quad VB from PadPose
xrEndFrame:
  layer = XrCompositionLayerPassthroughFB  (background)
  layer = XrCompositionLayerProjection     (glow quads)
```

Reference: [Meta XrPassthrough sample](https://github.com/meta-quest/Meta-OpenXR-SDK/tree/main/Samples/XrSamples/XrPassthrough/Src)

**Glow placement:** World-space circle at `PadPose.position` with normal
`PadPose.normal`. MVP = projection × view × world. **No screen-space HUD.**

---

### Module: `midi` ✅


| Tool                                                                | Function                                              | Status      |
| ------------------------------------------------------------------- | ----------------------------------------------------- | ----------- |
| [RtMidi 6.0.0](https://github.com/thestk/rtmidi/releases/tag/6.0.0) | `RtMidiIn`, `openPort`, `setCallback`                 | ✅           |
| Simmons map                                                         | `simmonsTitan50B_EX::getDrumPad(note)`                | ✅ 14 pads   |
| Deferred                                                            | `steady_clock` timestamps, lock-free `HitEvent` queue | Milestone 8 |


---

### Calibration Harness (`calibration/`) — Milestone 1c

**Purpose:** Capture synchronised stereo checkerboard frame pairs to compute stereo
extrinsics **before M4 stereo triangulation**. Without this, `StereoTriangulation.cpp`
will produce systematically wrong depths regardless of intrinsics quality.

**When:** Complete during **M1c** (after dual-port stereo stream works). M4 depends on
`stereo_calib.json` already existing.

**Workflow:**

1. Run `capture_stereo_pairs.exe` while both eyes stream — saves `left_NNNN.png` /
  `right_NNNN.png` pairs when a keypress is detected (≥ 20 pairs recommended)
2. Print a 9×6 asymmetric checkerboard at known square size
3. Run `calibrate_stereo.py`:

```python
# calibrate_stereo.py (offline, runs on Python with OpenCV)
ret, K_l, D_l, K_r, D_r, R, t, E, F = cv2.stereoCalibrate(
    objpoints, imgpoints_l, imgpoints_r,
    K_l_init, D_l_init, K_r_init, D_r_init,
    img_size,
    flags=cv2.CALIB_FIX_INTRINSIC   # intrinsics already from PCA API
)
R1, R2, P1, P2, Q, _, _ = cv2.stereoRectify(K_l, D_l, K_r, D_r,
                                              img_size, R, t)
# Save K_l, D_l, K_r, D_r, R, t, R1, R2, P1, P2, Q → stereo_calib.json
```

**Output:** `stereo_calib.json` — loaded once at `VRDrummer.exe` startup by
`StereoCalib.cpp`.

---

### Module: `timing` — Deferred (Gameplay)


| Tool            | Link                                                                                                | When         |
| --------------- | --------------------------------------------------------------------------------------------------- | ------------ |
| Hardcoded chart | C++ struct array                                                                                    | Milestone 9  |
| `.mid` parse    | [libsmf](https://github.com/jmckysps5/libsmf) or [midifile](https://github.com/craigsapp/midi-file) | Milestone 12 |
| Tempo map       | Custom tick→ms from MIDI meta                                                                       | Milestone 12 |


---

## 6. Phased Adoption Tiers

Do **not** implement all upgrades at once.


| Tier                     | Scope                     | Included                                                                                                                                             |
| ------------------------ | ------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Tier 1 — Bootstrap**   | M1a–M3 working end-to-end | camera-fgs mono H.264, adb TCP, FFmpeg CPU, OpenCV CPU, YOLO11n-seg ONNX, mono 3D                                                                    |
| **Tier 2 — Performance** | M3–M5 polish              | Stereo both eyes, NVDEC zero-copy, cv::cuda + CLAHE, TensorRT FP16→INT8, ByteTrack, HEVC fork, EKF stereo + hand tracking prior, `XR_FB_color_space` |
| **Tier 3 — Transport**   | M7                        | Wi-Fi WebRTC path (constrained baseline + no jitter buffer), optional UDP/RTP USB fork                                                               |
| **Tier 4 — Gameplay**    | M8–M12                    | MIDI merge, charts, scoring, hand tracking strike prediction                                                                                         |
| **Research**             | Post-MVP                  | AoA direct bulk USB                                                                                                                                  |


---

## 7. Build Roadmap

**Estimate:** Perception MVP (M0–M7) ≈ 5–8 months; Gameplay (M8–M12) ≈ 3–5 months after.


| #      | Milestone                                     | Key deliverable                                                         | Tier | Duration |
| ------ | --------------------------------------------- | ----------------------------------------------------------------------- | ---- | -------- |
| 0      | MIDI ✅                                        | RtMidi 14-pad map                                                       | —    | Done     |
| 0b     | OpenXR probe                                  | Instance + passthrough proc load                                        | —    | Partial  |
| **1a** | **USB camera stream (bootstrap)**             | Left eye, H.264, CPU decode, ffplay verify                              | 1    | 3–5 wk   |
| 1b     | NVDEC + zero-copy                             | NVDEC → NV12 → cv::cuda BGR; ORT IOBinding (Tier 1b)                    | 2    | 2–3 wk   |
| 1c     | Stereo dual-port + metadata + **calibration** | Both eyes, timestamp sync, intrinsics side channel, `stereo_calib.json` | 2    | 2–3 wk   |
| **1d** | **Wi-Fi WebRTC stream**                       | QuestCameraKit → libdatachannel; CBP + no jitter buffer                 | 3    | 3–5 wk   |
| 2      | Kit detection                                 | OpenCV contours or YOLO `drum_kit`                                      | 1    | 2–4 wk   |
| 3      | Pad ID 14-class seg                           | YOLO11n-seg ONNX; CVAT labels                                           | 1→2  | 4–8 wk   |
| 3b     | TensorRT engine                               | FP16 then INT8 with calibration                                         | 2    | 1–2 wk   |
| 3c     | ByteTrack integration                         | Hungarian + IoU; filter spurious detections                             | 2    | 1–2 wk   |
| 4      | EKF + stereo fusion                           | Eigen3; `xrLocateSpace`; stereo triangulation                           | 2    | 3–5 wk   |
| 5      | MR glow overlays                              | XrPassthrough + D3D11; `XR_FB_color_space`                              | 2    | 3–5 wk   |
| 6      | Robustness                                    | CLAHE, EKF noise tuning, dropout recovery                               | 2    | 3–5 wk   |
| 7      | Record/replay tooling                         | ImGui replay; optional QuestRealityCapture log format                   | 2    | 2–3 wk   |
| 8      | MIDI merge                                    | RtMidi lock-free queue in unified app                                   | 4    | 1–2 wk   |
| 9      | Chart + predictive glow                       | Hand tracking wrist prior into EKF                                      | 4    | 2–3 wk   |
| 10     | Scoring flashes                               | ±25 ms green/red/purple                                                 | 4    | 1–2 wk   |
| 11     | Latency lab                                   | E2E ≤ 30 ms strike-to-flash; ImGui HUD                                  | 4    | 1–2 wk   |
| 12     | `.mid` / `.pdf` import                        | libsmf / PDF viewer                                                     | 4    | 4–8 wk   |


### Milestone 1a Acceptance Tests


| Test                                  | Pass criterion                                             |
| ------------------------------------- | ---------------------------------------------------------- |
| camera-fgs + Link + PC app concurrent | No crash for 10 min                                        |
| Left eye ≥ 15 fps decoded for 60 s    | CPU FFmpeg OK                                              |
| ffplay recipe works                   | [camera-fgs README](https://github.com/t-34400/camera-fgs) |
| PCA resolution probed                 | 1280×960 and 1280×1280 both tested; best chosen            |
| Latency documented                    | PCA capture + encode + adb + decode each measured          |


### Milestone 1c Acceptance Tests


| Test                          | Pass criterion                                                 |
| ----------------------------- | -------------------------------------------------------------- |
| Both eyes stream concurrently | Left + right ≥ 15 fps each for 60 s                            |
| Metadata port `18090`         | Intrinsics + pose JSON received per frame                      |
| Stereo timestamp sync         | Left/right offset ≤ 33 ms (1 frame @ 30 fps)                   |
| Calibration harness           | ≥ 20 checkerboard pairs captured; `stereo_calib.json` produced |
| `StereoCalib.cpp` loads JSON  | Rectify maps build without error at startup                    |


### Milestone 1d Acceptance Tests


| Test                                   | Pass criterion                                        |
| -------------------------------------- | ----------------------------------------------------- |
| QuestCameraKit → libdatachannel PC     | ≥ 15 fps                                              |
| Constrained Baseline Profile confirmed | SDP `profile-level-id=42e01f` verified in negotiation |
| Jitter buffer disabled                 | p95 latency ≤ 2× p50 latency                          |
| Same CV pipeline as USB                | Bit-identical detector output ±1 frame                |
| Jitter measured                        | Document p50/p95 vs USB baseline                      |


---

## 8. Latency and Bandwidth Budget

### CV Pipeline Latency (USB, Target Tier 2)


| Stage                 | Bootstrap (Tier 1)  | Target (Tier 2) | Notes                                                                             |
| --------------------- | ------------------- | --------------- | --------------------------------------------------------------------------------- |
| PCA capture           | ~33 ms              | ~33 ms          | Fixed at ~30 fps                                                                  |
| Quest HW encode       | ~5–15 ms            | ~5–8 ms         | HEVC + `KEY_LATENCY=0`                                                            |
| USB / adb transport   | ~1–5 ms             | ~1–3 ms         | `adb reverse` optional                                                            |
| PC NVDEC decode       | 5–8 ms (CPU)        | **< 2 ms**      | NVDEC + zero-copy                                                                 |
| NV12 → BGR (cv::cuda) | 1–3 ms (CPU)        | **< 0.5 ms**    | On-GPU colour convert                                                             |
| CLAHE                 | N/A                 | **< 1 ms**      | cv::cuda on 4080 Super                                                            |
| YOLO inference        | 15–30 ms (ORT FP32) | **3–8 ms**      | TensorRT INT8                                                                     |
| ByteTrack             | ~0.5 ms             | ~0.5 ms         | CPU; negligible                                                                   |
| EKF                   | < 1 ms              | < 1 ms          | Eigen3                                                                            |
| **CV total**          | **~60–100 ms**      | **~45–55 ms**   | Inference path only; full glass-to-glow may be ~70–90 ms until transport upgrades |


### Display Path (unchanged)


| Stage                          | Budget                                            |
| ------------------------------ | ------------------------------------------------- |
| Glow render + OpenXR submit    | 11 ms (90 Hz)                                     |
| Link encode / decode           | 5–15 ms                                           |
| **Pose update rate for glows** | Match CV output (~10–30 Hz); interpolate at 90 Hz |


### Bandwidth


| Stream                       | Bitrate                                        |
| ---------------------------- | ---------------------------------------------- |
| Mono H.264 1280×960 @ 30 fps | ~8–15 Mbps                                     |
| Stereo HEVC Tier 2           | ~10–20 Mbps                                    |
| Glow / pose data             | Internal PC memory only                        |
| Video back to Quest          | **0** — passthrough compositor handles display |


### Strike Timing (Gameplay Phase Only)

MIDI scoring target: **≤ 30 ms** strike-to-flash. CV pipeline latency does not need
to meet this until chart-driven predictive glow ships in Milestone 9+. Hand tracking
wrist prior (M9) improves predictive accuracy without requiring lower CV latency.

---

## 8.1 Additional Performance Optimizations

Optional upgrades beyond Tier 2 baseline — ordered by ROI. Adopt incrementally after
M1b–M3 acceptance tests pass.

### High ROI (PC pipeline)


| #   | Optimization                                                                                               | Benefit                                      |
| --- | ---------------------------------------------------------------------------------------------------------- | -------------------------------------------- |
| 1   | **CUDA streams / pipeline overlap** — decode frame N+1 while inferring frame N on separate `cudaStream_t`  | Hides 30–50% of serial decode+infer time     |
| 2   | **ROI crop before YOLO** — after kit detector, run seg model only on drum-rack crop (~50% pixels)          | Often 2–3× faster inference                  |
| 3   | **Fixed input size + letterbox cache** — precompute letterbox transform; reuse GPU buffer each frame       | Removes per-frame alloc overhead             |
| 4   | **TensorRT FP16 before INT8** — validate `half=True` engine first; INT8 only if FP16 headroom insufficient | Faster iteration; fewer calibration failures |


### Medium ROI (transport / estimation / render)


| #   | Optimization                                                                          | Benefit                                               |
| --- | ------------------------------------------------------------------------------------- | ----------------------------------------------------- |
| 5   | **Dedicated metadata UDP port** — tiny JSON on separate socket; never block video TCP | Isolates side-channel from H.264 stream               |
| 6   | **TCP_NODELAY + small recv buffer** on `UsbTcpTransport`                            | Lower adb-forward latency (see §4)                    |
| 7   | **Stereo at 15 fps per eye** if 30 fps stereo overloads encode                        | Pads are static; half rate may suffice                |
| 8   | **JPEG debug ports** (`19091`/`19092` in camera-fgs)                                  | ImGui preview without touching H.264 path             |
| 9   | **xrTimeToDisplayTime interpolation** — glow placement at predicted display time    | Reduces visual lag (see §5 `estimation`)              |
| 10  | **Lightweight temporal filter** — EMA on pad positions (α≈0.3) before EKF             | Simpler bootstrap than full ByteTrack for static pads |
| 11  | **Adaptive CLAHE** — enable only when frame luminance variance drops below threshold  | Saves GPU in well-lit rooms                           |


### Lower ROI / research


| #   | Optimization                       | Notes                                                                                         |
| --- | ---------------------------------- | --------------------------------------------------------------------------------------------- |
| 12  | NVDEC → CUDA → D3D11 shared handle | If glow compositing needs GPU texture without readback                                        |
| 13  | QuestRealityCapture log format     | [QuestRealityCapture](https://github.com/t-34400/QuestRealityCapture) for M7 replay alignment |
| 14  | AoA direct bulk USB                | Deferred; see Open Questions                                                                  |


### Upgrade Validation (claims vs facts)


| Claim                                                         | Status                                                                     |
| ------------------------------------------------------------- | -------------------------------------------------------------------------- |
| PCA frames available to PC OpenXR app via `XR_FB_passthrough` | **False** — compositor display only; use camera-fgs or WebRTC              |
| camera-fgs uses software H.264 encode                         | **False** — already HW MediaCodec; Tier 2 = HEVC + tuning                  |
| Meta PCA API provides stereo extrinsics                       | **False** — per-camera intrinsics only; need `cv::stereoCalibrate` harness |
| TensorRT IOBinding == ORT IOBinding                           | **False** — Tier 2 uses native TRT `enqueueV3` bindings                    |
| Tier 2 CV total ~45–55 ms = end-to-end glass-to-glow          | **Partial** — inference path only; full loop ~70–90 ms typical             |


---

## 9. Pedagogy and Codebase

### AI Tutor Model

- **Tutor, not author** — pseudocode first; you implement and debug
- Per topic: Prerequisites → Intuition → Math → Procedure → Verification
- When stuck: guiding questions first, not full solutions

### Learning Tracks


| Track              | Topics                                                | Milestones      |
| ------------------ | ----------------------------------------------------- | --------------- |
| **A — Streaming**  | PCA, H.264/HEVC, FFmpeg, NVDEC, TCP/WebRTC            | 1a–1d           |
| **B — CV/ML**      | Labelling, YOLO, ONNX, TensorRT, ByteTrack, 2D→3D     | 2–3c            |
| **C — Estimation** | EKF, sensor fusion, stereo triangulation              | 4               |
| **D — OpenXR/MR**  | Passthrough layers, D3D11, color space, hand tracking | 5               |
| **E — Timing**     | MIDI, charts, latency                                 | 8–12 (deferred) |


### Current Codebase


| File                                  | Status     | Notes                                        |
| ------------------------------------- | ---------- | -------------------------------------------- |
| `[midiTest.cpp](../midiTest.cpp)`     | ✅ Working  | RtMidi; 14-pad Simmons map; Note-On callback |
| `[vrTest.cpp](../vrTest.cpp)`         | ⚠️ Partial | Instance + `xrCreatePassthroughFB` proc load |
| `[CMakeLists.txt](../CMakeLists.txt)` | ✅          | FetchContent: OpenXR 1.1.59.1, RtMidi 6.0.0  |


```powershell
mkdir build && cd build
cmake ..
cmake --build . --config Release
# Outputs: midiTest.exe, vrTest.exe
```

### Simmons MIDI Map


| Note   | Pad                       |
| ------ | ------------------------- |
| 36     | Kick Drum                 |
| 38     | Snare Centre              |
| 40     | Snare Rim                 |
| 41–48  | Toms / Hi-Hat variants    |
| 49, 57 | Crash / Crash 2           |
| 51     | Ride                      |
| 85–86  | Hi-Hat Splash / Semi-Open |


### Thread Architecture (AMD 7800X3D)

Pin threads to separate physical cores to avoid cache contention on the 3D V-Cache:


| Thread              | Role                                     | Priority |
| ------------------- | ---------------------------------------- | -------- |
| `stream_thread`     | TCP recv + NVDEC decode                  | HIGH     |
| `cv_thread`         | CLAHE + YOLO + ByteTrack + triangulation | HIGH     |
| `estimation_thread` | EKF update                               | NORMAL   |
| `xr_thread`         | OpenXR frame loop + D3D11 render         | REALTIME |
| `midi_thread`       | RtMidi callback (Milestone 8)            | HIGH     |


Use `SetThreadAffinityMask` (Windows) to assign each thread to a distinct core.
Avoid placing `xr_thread` and `cv_thread` on the same physical core.

---

## 10. Open Questions


| #   | Question                                                                              | Revisit                                                                                                                                                                      |
| --- | ------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | Does 1280×1280 cover all 14 pads (especially kick + floor toms) from seated position? | Milestone 1a                                                                                                                                                                 |
| 2   | Does camera-fgs stereo (ports 8081+8082) survive Link active on HzOS v74–v83?         | Milestone 1c spike                                                                                                                                                           |
| 3   | HEVC hardware encode available on Snapdragon XR2 Gen 2 in camera-fgs fork?            | Milestone 1c                                                                                                                                                                 |
| 4   | Link Beta tab missing on Link v83 — registry workaround?                              | [Meta forum thread](https://communityforums.atmeta.com/discussions/Questions_Discussions/beta-tab--passthrough-toggle-missing-in-meta-horizon-link-v83---wired-link/1360010) |
| 5   | AoA bulk USB — is Quest 3S firmware compatible as USB accessory?                      | Post-MVP research                                                                                                                                                            |
| 6   | WebRTC jitter buffer disable in libdatachannel — which config field?                  | Milestone 1d                                                                                                                                                                 |
| 7   | Custom YOLO vs classical CV for v1 pad ID baseline?                                   | Milestone 2                                                                                                                                                                  |
| 8   | `.pdf` scope — visual reference only or parsed chart data?                            | Milestone 12                                                                                                                                                                 |
| 9   | `XR_COLOR_SPACE_RIFT_CV1_FB` on Quest 3S — correct color space at runtime?            | Milestone 5                                                                                                                                                                  |


---

## 11. Glossary


| Term                     | Definition                                                                                                |
| ------------------------ | --------------------------------------------------------------------------------------------------------- |
| **PCA**                  | Passthrough Camera API — raw Quest RGB via Android Camera2                                                |
| **NVDEC**                | NVIDIA dedicated hardware video decode unit on RTX GPU                                                    |
| **NV12**                 | YUV 4:2:0 planar format output by NVDEC; converted to BGR before inference                                |
| **Zero-copy**            | NVDEC → BGR → inference entirely on GPU; no `cudaMemcpy` to host                                          |
| **IOBinding**            | ONNX Runtime API for binding pre-allocated CUDA tensors (Tier 1b); distinct from TensorRT native bindings |
| **ByteTrack**            | Multi-object tracker using Hungarian algorithm + IoU; filters spurious YOLO detections                    |
| **CLAHE**                | Contrast Limited Adaptive Histogram Equalisation; GPU-based contrast normalisation                        |
| **Stereo rectification** | Warping stereo image pairs so corresponding points lie on the same scanline                               |
| **XR_FB_color_space**    | OpenXR extension to declare projection layer colour space; prevents hue shift vs passthrough              |
| **XR_EXT_hand_tracking** | OpenXR extension providing wrist / finger joint poses at 60–90 Hz                                         |
| **IVideoTransport**      | C++ abstract interface; `UsbTcpTransport` and `WifiWebRtcTransport` implement it                          |
| **Latest-frame-wins**    | Ring buffer policy; old frames are dropped; CV always sees the most recent frame                          |
| **Path A / B**           | USB adb TCP / Wi-Fi WebRTC camera transport paths                                                         |
| **Tier 1–4**             | Bootstrap → performance → Wi-Fi → gameplay adoption phases                                                |
| **PadPose**              | World-space position + normal + radius for one pad                                                        |
| **EKF**                  | Extended Kalman Filter for pad pose smoothing and sensor fusion                                           |
| **AoA**                  | Android Open Accessory — direct USB bulk transfer bypassing adb daemon                                    |
| **CBP**                  | H.264 Constrained Baseline Profile; mandatory for low-latency WebRTC decode                               |
| **XR_FB_passthrough**    | OpenXR extension for compositor passthrough display (no raw pixels to app)                                |
| **Link**                 | Wired USB-C connection running PC OpenXR on Quest                                                         |


---

## Document History


| Version | Date         | Summary                                                                                                                                                                                                                                                                                                                                                    |
| ------- | ------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1.0     | Jun 2026     | Initial consolidated doc                                                                                                                                                                                                                                                                                                                                   |
| 1.1     | Jun 2026     | CV-first roadmap reprioritisation                                                                                                                                                                                                                                                                                                                          |
| 2.0     | Jun 2026     | Option A; dual-pipeline; camera-fgs primary; exact packages and APIs                                                                                                                                                                                                                                                                                       |
| 3.0     | Jun 2026     | Upgrade validation; Tier 1–4 adoption; USB/Wi-Fi pathways; NVDEC/TensorRT/stereo target                                                                                                                                                                                                                                                                    |
| **4.0** | **Jun 2026** | **Full gap closure:** NVDEC zero-copy code path; IVideoTransport contract; stereo rectification + calibration harness; ByteTrack/SORT tracker; CLAHE preprocessing; 1280×1280 resolution probe; WebRTC CBP + jitter buffer tuning; AoA research entry; XR_FB_color_space; XR_EXT_hand_tracking EKF prior; thread pinning table; all open questions updated |
| **4.1** | **Jun 2026** | **Review fixes:** removed paste corruption; aligned Phase 1a–1d with roadmap; moved calibration harness to M1c; split ORT IOBinding vs TensorRT native bindings; metadata port 18090; `xrTimeToDisplayTime`; additional optimizations + upgrade validation table                                                                                           |





# Version 5.0 Upgrade Notes (Applied Review)

## New Architectural Principles

### Record → Replay → Benchmark First
Add a dedicated subsystem before advanced perception work.

Capabilities:
- Raw video recording
- Metadata recording
- MIDI recording
- Full replay mode
- Regression datasets
- Benchmark runs

Repository additions:

```text
recording/
├── VideoRecorder.cpp
├── MetadataRecorder.cpp
├── MidiRecorder.cpp
├── ReplaySession.cpp
└── BenchmarkRunner.cpp
```

---

### Canonical Coordinate Frames

Define all frames explicitly:

```text
QuestTrackingFrame
LeftCameraFrame
RightCameraFrame
WorldFrame
DrumKitFrame
PadFrame
```

Every transform in the system must be documented.

---

### Filter Strategy Architecture

Replace EKF-only design with:

```cpp
class IPoseFilter;
class EkfPoseFilter;
class UkfPoseFilter;
class FixedPoseFilter;
```

Allows objective comparison of estimation methods.

---

### Unified Time System

Create a single timestamp domain covering:

```text
Capture
Encode
Transport
Decode
Inference
Render
MIDI
```

All latency measurements derive from this clock.

---

### Configuration System

Add:

```text
config/
└── config.yaml
```

Examples:

```yaml
yolo_threshold: 0.5
stereo_fps: 30
ekf_process_noise: 0.01
```

---

### Runtime Health Monitoring

Track:

- Dropped frames
- Decode failures
- Stereo synchronization offset
- Tracker confidence
- End-to-end latency

---

### Detector Abstraction

```cpp
class IDetector;
class YoloDetector;
class TensorRtDetector;
```

Prevents detector lock-in.

---

### Simulation Mode

Support:

- Recorded camera streams
- Synthetic detections
- Synthetic MIDI

without a Quest headset attached.

---

## Roadmap Adjustments

Insert immediately after M1c:

### M1e — Dataset & Replay Infrastructure

Deliverables:

- Recording pipeline
- Replay player
- Benchmark harness
- Dataset format definition

This milestone reduces debugging effort throughout the rest of the project.

---

## Documentation Cleanup

Completed recommendations:

- Standardize terminology around Milestones and Tiers.
- Remove duplicated historical content when merging future versions.
- Keep acceptance tests adjacent to milestone definitions.
- Keep open questions attached to the relevant milestone where possible.
