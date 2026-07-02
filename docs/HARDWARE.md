# VRDrummer — Reference Hardware Build

This file records the **specific hardware** used to develop VRDrummer. The architecture
document ([DESIGN.md](DESIGN.md)) is hardware-agnostic; this sheet is for day-to-day setup only.

Substitutes are fine if they meet the minimum capabilities in DESIGN §2.

---

## PC

| Component | Model | Notes |
|-----------|-------|-------|
| CPU | AMD Ryzen 7 7800X3D | Pin CV and render threads to separate cores |
| GPU | NVIDIA RTX 4080 Super | CUDA, NVDEC (H.264 + HEVC), TensorRT |
| OS | Windows 11 | OpenXR PC runtime via Meta Link |
| RAM | 32 GB recommended | Replay datasets + training |

---

## VR Headset

| Component | Model | Notes |
|-----------|-------|-------|
| Headset | Meta Quest 3S | Developer mode enabled |
| OS | Horizon OS v74+ | PCA resolution probe at 1280×960 and 1280×1280 |
| MR link | Wired USB-C ≥ 2 Gbps | Keep wired during Wi-Fi CV experiments |

---

## Drum Kit

| Component | Model | Notes |
|-----------|-------|-------|
| Kit | Simmons Titan 50 B-EX | 14 pads, USB MIDI |
| Sticks | Real acoustic-style sticks | Not VR controllers |

### MIDI Note Map (Simmons Titan 50 B-EX)

Load from `config/kits/simmons_titan_50.yaml` in the unified app. Reference mapping:

| Note | Pad |
|------|-----|
| 36 | Kick Drum |
| 38 | Snare Centre |
| 40 | Snare Rim |
| 41 | Tom 4 |
| 42 | Hi-Hat Closed |
| 43 | Tom 3 |
| 44 | Hi-Hat Pedal |
| 45 | Tom 2 |
| 46 | Hi-Hat Open |
| 48 | Tom 1 |
| 49 | Crash |
| 51 | Ride |
| 57 | Crash 2 |
| 85 | Hi-Hat Splash |
| 86 | Hi-Hat Semi-Open |

---

## Companion Streaming (Reference)

| Item | Value |
|------|-------|
| App | [camera-fgs](https://github.com/t-34400/camera-fgs) APK |
| Left video | `adb forward tcp:18081 tcp:8081` |
| Right video | `adb forward tcp:18082 tcp:8082` |
| Metadata JSON | `adb forward tcp:18090 tcp:8090` (if enabled in fork) |
| JPEG debug | ports 19091 / 19092 (optional preview) |

---

## Software Versions (Reference Build)

| Package | Version |
|---------|---------|
| CMake | ≥ 3.20 |
| C++ | C++17 |
| OpenXR SDK | 1.1.59.1 |
| RtMidi | 6.0.0 |
| FFmpeg | BtbN build with `--enable-nvdec` |
| OpenCV | Built with CUDA |
| ONNX Runtime | CUDA EP |
| TensorRT | Match GPU driver |
