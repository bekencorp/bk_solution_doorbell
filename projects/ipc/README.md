# IPC Project (BK7259 SMP Solution)

* [中文](./README_CN.md)

## 1 Overview

This project is the **IPC network camera firmware** in the doorbell solution, based on BK7259 SMP.
It reuses the `smart_lock` doorbell stack (BLE provisioning, TCP/UDP streaming, full-duplex audio, ASR),
but **does not bring up the LCD/GPU display pipeline by default** — targeting headless IPC / network camera products.
It now also enables AP-powerdown low-power keepalive by reusing the `doorbell_lp` keepalive path.

### 1.1 vs doorbell

| Item | doorbell | ipc (this project) |
| --- | --- | --- |
| Use case | video doorbell with LCD | headless IPC camera |
| Default sensor | GC2053 1920×1080@25fps | SC3336 2304×1296@20fps |
| LCD / GPU / DPU | enabled in ap_main | not configured in ap_main (LCD/GPU drivers remain in defconfig) |
| AT commands | port 5 | port 5, WiFi/MISC AT |
| ASR | auto-start | auto-start |
| UVC | defconfig enabled | disabled in defconfig (ap_main configures MIPI only) |
| CS2 P2P | enabled | not enabled (CS2 buffer config retained) |
| Low-power keepalive | supported by doorbell_lp | enabled for headless IPC |

## 2 Features

| Module | Description |
| --- | --- |
| Camera | MIPI CSI (default SC3336, 2304×1296@20fps, NV12, Flexa) |
| Codec | H.264 HW encoder, JPEG decoder |
| Network | LAN UDP/TCP streaming (doorbell stack) |
| Bluetooth | BLE Boarding provisioning |
| Audio | ADK + AEC v3 + G.711/G.722, on-board Mic/Speaker, UVC UAC |
| ASR | KWS + TFLite-Micro + NPU (SRAM mode, `CONFIG_ASR_SERVICE_USE_SRAM`) |
| AT | port 5, `CONFIG_WIFI_AT_ENABLE` / `CONFIG_MISC_AT_ENABLE` |
| Low-power keepalive | AP powers down after multimedia idle; CP keeps TCP keepalive and uses RTC wakeups to monitor server responses |

### 2.1 Default board config

Pin definitions in `ap/ap_main.c` (same MIPI pins as doorbell):

| Signal | GPIO | Notes |
| --- | --- | --- |
| MIPI I2C SCL | GPIO_69 | bus id = 1 |
| MIPI I2C SDA | GPIO_70 | |
| MIPI Sensor Reset | GPIO_71 | |
| MIPI Sensor XCLK | GPIO_59 | |

**Camera (MIPI CSI)**

- Sensor: SC3336 (`CONFIG_CSI_SC3336`)
- Max resolution: 2304×1296@20fps
- ISP main channel: 2304×1296, NV12, Flexa enabled
- No display / gpu board init

### 2.2 Video pipeline

```
SC3336 (MIPI CSI, 2304×1296 NV12 Flexa)
    │
    └──► H.264 Encoder (Flexa bond) ──► Wi-Fi stream
```

No LCD branch. H.264 encode/stream follows ISP MP board config (2304×1296).

### 2.3 Network

Same ports as doorbell: CTRL 7100 / VIDEO 7150 TCP, 7180 UDP / AUDIO 7140 TCP, 7170 UDP.
Demo uses BekenIot APK: after provisioning, H.264 stream starts automatically (2304×1296).

### 2.4 AT commands

AT subsystem enabled in defconfig, default port 5 (`CONFIG_DEFAULT_AT_PORT=5`).
Used for production test automation (Wi-Fi connect, device info query, etc.).

## 3 Build

### 3.1 Dependency

AVDK SDK (`avdk_sdk`), specified via `SDK_DIR`.

### 3.2 Build command

```bash
cd projects/ipc
SDK_DIR=/abs/path/to/avdk_sdk ./dbuild.sh make bk7259 PROJECT=ipc
```

Local build:

```bash
make bk7259 SDK_DIR=/abs/path/to/avdk_sdk PROJECT=ipc
```

### 3.3 Output

```
projects/ipc/build/bk7259/ipc/package/all-app.bin
```

## 4 Flash and Demo

### 4.1 Hardware

- BK7259 development board
- MIPI CSI camera (default SC3336, 2304×1296)
- On-board speaker / mic
- No LCD required

### 4.2 Demo flow

Same as doorbell, using BekenIot APK:

1. Install APK → sign up / log in → add `Video Doorbell` device
2. BLE provisioning (2.4G Wi-Fi)
3. H.264 stream starts automatically after provisioning
4. Toggle audio from APK (no LCD button)
5. Turn off streaming/audio and wait for the idle check; the device enters CP keepalive low-power state and wakes AP on server wakeup or keepalive failure

### 4.3 Use cases

- Indoor/outdoor IPC camera (no local display)
- Batch production test with AT commands
- High-resolution sensor (2304×1296) streaming validation
- Keyword wake-up (ASR) + remote talk

## 5 Layout

```
projects/ipc
├── ap/
│   ├── ap_main.c                 # camera board config only
│   ├── audio_param/audio_para.c
│   └── config/bk7259_ap/defconfig
├── cp/
│   ├── cp_main.c
│   ├── db_ipc_msg/                # AP/CP keepalive command channel
│   ├── db_pack/                   # CP keepalive TCP packet framing
│   ├── keepalive/                 # CP-side low-power keepalive
│   ├── powerctrl/                 # AP power control and low-voltage sleep
│   └── config/bk7259/defconfig
├── partitions/bk7259/
├── CMakeLists.txt
├── Makefile
├── dbuild.sh / dbuild.ps1
└── .ci
```

### 5.1 Key defconfig differences vs doorbell

| Config | ipc | doorbell |
| --- | --- | --- |
| `CONFIG_CSI_SC3336` | y | n |
| `CONFIG_CSI_GC2053` | n | y |
| `CONFIG_APP_GPU` | n | y |
| `CONFIG_CS2_P2P_SERVER` | n | y |
| `CONFIG_ASR_SERVICE_USE_SRAM` | y | n |
| `CONFIG_AT` / `CONFIG_WIFI_AT_ENABLE` | y | y |
| `CONFIG_NTWK_CLIENT_SERVICE_ENABLE` | y | y (doorbell_lp) |
| `CONFIG_PM_ONLY_CP_ENABLE` | y (CP) | y (doorbell_lp CP) |
| `CONFIG_MDS_SNAPSHOT` | y | y |

Shared components: `../../components/`.

## 6 FAQ

**Q: Can I add an LCD?**

A: defconfig retains LCD driver options, but `ap_main.c` has no display/gpu config. Use the `doorbell` project or add display init in ap_main.

**Q: How to change sensor?**

A: Edit resolution/pin params in `ap/ap_main.c` and switch `CONFIG_CSI_*` in defconfig.

**Q: What is the stream resolution?**

A: Same as capture: 2304×1296. `app_h264e_turn_on()` reads ISP MP channel size; APK `DBCMD_SET_CAMERA_TURN_ON` width/height are not applied on the MIPI path.
