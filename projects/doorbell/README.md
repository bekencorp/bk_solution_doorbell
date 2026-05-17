# Doorbell Project (BK7259 SMP Solution)

* [中文](./README_CN.md)

## 1 Overview

This project is the BK7259 SMP port of `bk_avdk_smp_dev_7259v2_bringup_25W4801/projects/multimedia/doorbell`.
It keeps the full doorbell capability set: UVC/MIPI camera capture, H.264 encoding, real-time Wi-Fi A/V
transmission, BLE provisioning, full-duplex audio and ASR keyword wake-up.

CPU split:

- **CP**: only `bk_init` + `bk_start_ap_system()` to bring up the AP.
- **AP**: runs all business logic (`media_service`, `devices_mgmt`, `smart_lock` doorbell core/boarding/network, ASR, etc.).

## 2 Highlights

| Module | Description |
| --- | --- |
| Camera | UVC + MIPI ISP (default 1920x1080@25fps, NV12) |
| Display | MIPI DSI (default `lcd_device_hx8399c_mipi_1080x1920`), DPU video channel ARGB8888 |
| GPU | VG-Lite Flexa rotate / format convert / compression |
| Codec | H.264 hardware encoder (doorbell), SW/HW JPEG decoder |
| Network | LAN UDP/TCP, CS2 P2P, real-time Wi-Fi streaming |
| Bluetooth | BLE Boarding provisioning, BLE Slave-only |
| Audio | ADK + AEC v3 + G.711/G.722, on-board Mic/Speaker, UVC UAC |
| ASR | KWS + TFLite-Micro + NPU |

See `ap/config/bk7259_ap/defconfig` for the full configuration.

## 3 Build

The project depends on the AVDK source tree `bk_avdk_smp_dev_7259v2_bringup_25W4801`. The SDK must sit
next to this solution (sibling directory) or be specified via the `SDK_DIR` environment variable. The
recommended entry point is the solution `dbuild.sh` wrapper, which sets `BK_SOLUTION_MODE=1`,
`SOLUTION_DIR` and `PROJECT_DIR` automatically:

```bash
cd projects/doorbell
./dbuild.sh make bk7259 PROJECT=doorbell
```

To pin the SDK path:

```bash
SDK_DIR=/abs/path/to/bk_avdk_smp_dev_7259v2_bringup_25W4801 ./dbuild.sh make bk7259 PROJECT=doorbell
```

Build output:

```
projects/doorbell/build/bk7259/doorbell/package/all-app.bin
```

Flash this single bin into BK7259v2.

## 4 Demo

1. Install the IOT APK: <https://dl.bekencorp.com/apk/BekenIot.apk>.
2. Sign up / log in.
3. Add device -> select `Video Doorbell`.
4. Pick a non-5G Wi-Fi, then proceed to BLE provisioning.
5. From the scanned BLE list, tap the entry whose IP matches the device. Provisioning auto-completes.
6. After provisioning, the camera + H.264 streaming starts automatically (864x480).
7. The on-board LCD and audio are toggled from the APK buttons.

> Make sure the UVC camera, MIPI LCD, speaker and microphone are connected before testing.

## 5 Layout

```
projects/doorbell
├── ap/                       # AP business (doorbell + media + ASR)
│   ├── ap_main.c
│   ├── CMakeLists.txt
│   └── config/bk7259_ap/defconfig
├── cp/                       # CP boot (only kicks the AP)
│   ├── cp_main.c
│   ├── CMakeLists.txt
│   └── config/bk7259/defconfig
├── partitions/bk7259/        # Flash partitions & RAM regions
│   ├── auto_partitions.csv
│   └── ram_regions.csv
├── CMakeLists.txt            # top-level; injects ../../components into EXTRA_COMPONENTS_DIRS
├── Makefile
├── dbuild.sh / dbuild.ps1    # docker build wrapper (solution mode)
└── .ci                       # CI compile command
```

Solution-side common components used by this project live in
`bk_solution_doorbell_dev_7259v2_bringup_25W4801/components/`:

- `multimedia_device_service`: camera/display/gpu/codec/uvc device management.
- `smart_lock`: doorbell_core, ble_boarding, network_transfer, audio_device, cmd, etc.
- `lcd_device`: solution-local ST7701SN RGB panel driver (opt-in).
