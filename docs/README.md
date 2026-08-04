# Doorbell Solution (BK7259)

* [中文](./README_CN.md)

## Overview

BK7259 SMP doorbell solution: video doorbell, low-power keepalive, headless IPC, and ISP/H264E tuning projects.

| Project | Description |
| --- | --- |
| `doorbell` | Video doorbell: MIPI/UVC, H.264 stream, MIPI LCD, BLE provisioning |
| `doorbell_lp` | doorbell + low-power keepalive |
| `ipc` | Headless IPC: SC3336 2304×1296 |
| `isp_h264_tuning` | ISP / H264E PC-side tuning over Wi-Fi |

Per-project details: `projects/<name>/README.md`.

## Build

The solution depends on the AVDK SDK (`bk_avdk_smp`). Place the SDK as a sibling directory or set `SDK_DIR`.

```bash
cd projects/doorbell
./dbuild.sh make bk7259 PROJECT=doorbell
```

Pin SDK path:

```bash
SDK_DIR=/abs/path/to/bk_avdk_smp ./dbuild.sh make bk7259 PROJECT=doorbell
```

Output: `projects/<name>/build/bk7259/<name>/package/all-app.bin`

## Demo

1. Install <https://dl.bekencorp.com/apk/BekenIot.apk>
2. Sign up / log in, add device → `Video Doorbell`
3. Pick 2.4G Wi-Fi → BLE provisioning
4. Camera + H.264 stream starts (doorbell default MIPI 1920×1080)
5. Toggle LCD and audio from APK buttons

> Connect MIPI camera, MIPI LCD, speaker and mic before testing. Plug UVC camera into USB if used.

## Documentation

Sphinx docs under `docs/bk7259/`. Generate HTML:

```bash
cd docs
make doc
```

Output: `docs/build/doc/bk7259/`.
