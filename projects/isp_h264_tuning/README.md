# ISP / H264E Tuning Project

* [中文](./README_CN.md)

## 1 Overview

This project (`isp_h264_tuning`) is a standalone **PC-side image quality tuning** firmware. It provides a unified preview service over Wi-Fi, supporting ISP raw frame capture and H264E encoded stream modes.

**Typical uses:**

- ISP tuning (AE/AWB/NR, etc.) with live NV12 preview on PC
- H264E parameter tuning (bitrate, QP, etc.) with live H.264 ES preview on PC
- Validate Flexa zero-copy ISP→H264E encode pipeline

### 1.1 Network architecture

PC and board communicate over Wi-Fi via a single `network_transfer` instance:

| Channel | Port | Protocol | Purpose |
| --- | --- | --- | --- |
| CTRL | TCP 7100 | JSON-RPC 2.0 | param query/set, start/stop |
| VIDEO | TCP 7150 | binary stream | H264 ES or ISP NV12 frames |

PC selects H264E or ISP module via JSON-RPC `method`. Only one module is active at a time.

### 1.2 Software components

| Component | Source file | Role |
| --- | --- | --- |
| `media_preview_server` | `ap/media_preview_server.c` | unified network service, RPC dispatch, module switch |
| `h264e_stream_project` | `ap/h264e_stream_project.c` | H264 encode session, `h264EScream.*` RPC |
| `isp_frame_project` | `ap/isp_frame_project.c` | ISP capture session, `ispFrame.*` RPC |

## 2 Default parameters

| Item | Default | Notes |
| --- | --- | --- |
| Resolution | 2304×1296 | switchable via CLI / RPC |
| FPS | 20 | |
| H264E bitrate | 1200 kbps | change via `setRateControl` |
| Sensors | GC2053 / CV2005 / SC3336 | all enabled in defconfig |
| Wi-Fi STA | UART CLI | `wifi_sta connect <ssid> <password>` |
| BLE / USB | disabled | Wi-Fi-only tuning project |

Supported resolutions: 2304×1296, 1920×1080, 1280×720 (CLI shorthand `1080p` / `720p`).

### 2.1 Default board config

MIPI pins (same as doorbell, see `h264e_stream_project.c` / `isp_frame_project.c`):

| Signal | GPIO |
| --- | --- |
| I2C SCL / SDA | GPIO_69 / GPIO_70 |
| Sensor Reset / XCLK | GPIO_71 / GPIO_59 |

### 2.2 H264E vs ISP pipeline

| Item | H264E mode | ISP mode |
| --- | --- | --- |
| ISP mode | Flexa (zero-copy) | Frame (non-Flexa) |
| Downstream | H264E encoder (Flexa bond) | direct NV12 output |
| RPC prefix | `h264EScream.*` / `h264e.*` | `ispFrame.*` / `isp.*` |
| VIDEO data | H.264 ES stream | NV12 raw frames |
| Typical use | encode quality / bitrate tuning | ISP image quality tuning |

## 3 Layout

```text
projects/isp_h264_tuning/
├── ap/
│   ├── ap_main.c                 # Wi-Fi CLI + preview service auto start
│   ├── media_preview_server.c    # unified RPC dispatch
│   ├── h264e_stream_project.c    # H264E module
│   ├── isp_frame_project.c       # ISP capture module
│   └── config/bk7259_ap/defconfig
├── cp/
│   ├── cp_main.c
│   └── config/bk7259/defconfig
├── partitions/bk7259/
├── CMakeLists.txt
├── Makefile
└── dbuild.sh
```

## 4 Build

```bash
cd projects/isp_h264_tuning
SDK_DIR=/abs/path/to/avdk_sdk ./dbuild.sh make bk7259 PROJECT=isp_h264_tuning
```

Output:

```
projects/isp_h264_tuning/build/bk7259/isp_h264_tuning/package/all-app.bin
```

## 5 Boot and PC connection

### 5.1 Auto start on power-up

1. `bk_init()` → `media_service_init()` → `devices_mgmt_init()`
2. Register CLI (h264e / isp / media_preview)
3. Auto connect STA Wi-Fi (params in `ap_main.c`)
4. Auto start `media_preview_server` (2304×1296@20fps)

### 5.2 UART log

```
media preview example ready.
  Unified server : default 2304x1296 @ 20fps
  PC selects H264E or ISP by JSON-RPC method
  CTRL:  TCP 7100 JSON-RPC (H264E or ISP methods)
  VIDEO: TCP 7150 H264 ES or ISP frames
  STA IP: x.x.x.x ...
```

### 5.3 PC connection steps

1. Ensure PC and board are on the same Wi-Fi network
2. Read board IP from UART log
3. PC tool connects to board_ip:7100 (CTRL) and board_ip:7150 (VIDEO)
4. Select module and control start/stop via JSON-RPC

## 6 CLI

```text
# Wi-Fi
wifi_sta connect MyRouter mypassword

# unified preview service
ap_cmd media_preview start 2304x1296 20
ap_cmd media_preview start 1080p 25          # shorthand resolution
ap_cmd media_preview stop

# standalone modules (when not using unified server)
ap_cmd h264e_stream server start 2304x1296 20
ap_cmd h264e_stream server stop
ap_cmd isp_frame server start 2304x1296 20
ap_cmd isp_frame server stop
```

## 7 JSON-RPC reference

### 7.1 Generic control

```json
{"jsonrpc":"2.0","method":"media.start","params":{},"id":1}
{"jsonrpc":"2.0","method":"media.stop","params":{},"id":2}
```

### 7.2 H264E mode

Query rate control:

```json
{"jsonrpc":"2.0","method":"h264EScream.getRateControl","params":{},"id":1}
```

Set bitrate and QP:

```json
{"jsonrpc":"2.0","method":"h264EScream.setRateControl","params":{"rateCtrl":{"bitrate":1200000,"qpMinI":18,"qpMaxI":40,"qpMinP":22,"qpMaxP":44}},"id":2}
```

Other supported method prefixes: `h264e.*`, `doorbell.encoder.*`, `encode_preview.*`, `start_encode`, `stop_encode`, `force_idr`, `get_rate_ctrl`, `set_rate_ctrl`.

### 7.3 ISP mode

```json
{"jsonrpc":"2.0","method":"ispFrame.start","params":{},"id":1}
{"jsonrpc":"2.0","method":"ispFrame.stop","params":{},"id":2}
{"jsonrpc":"2.0","method":"ispFrame.getConfig","params":{},"id":3}
{"jsonrpc":"2.0","method":"ispFrame.setConfig","params":{},"id":4}
{"jsonrpc":"2.0","method":"ispFrame.captureFrame","params":{},"id":5}
```

Other supported method prefixes: `isp.*`, `isp_capture.*`, `doorbell.isp.*`.

### 7.4 Module switch rules

- PC sends H264E method → activate H264E module, stop ISP module
- PC sends ISP method → activate ISP module, stop H264E module
- Send stop method (e.g. `stop_encode`, `ispFrame.stop`) → release current module
- Method mismatch with active module → JSON-RPC error `-32003`

## 8 Key defconfig

| Config | Description |
| --- | --- |
| `CONFIG_H264E_STREAM_SESSION` | H264E encode session |
| `CONFIG_ISP_FRAME_SESSION` | ISP capture session |
| `CONFIG_NTWK_CTRL_CHAN_JSON` | JSON-RPC control channel |
| `CONFIG_ENCODER_H264_CLK_240M` | H264E clock 240 MHz |
| `CONFIG_CJSON_USE` | cJSON parser |
| `# CONFIG_BLUETOOTH_AP` | BLE disabled |
| `# CONFIG_USB` | USB disabled |

## 9 FAQ

**Q: Wi-Fi connection fails?**

A: Run `wifi_sta connect <ssid> <password>` on UART. Verify credentials and that PC and board are on the same network.

**Q: PC connects to 7100 but no data on 7150?**

A: Send a start method first (e.g. `h264EScream.turnOn` or `ispFrame.start`) to start the media pipeline.

**Q: How to change resolution?**

A: Specify at CLI start (e.g. `media_preview start 1080p 25`), or stop and restart. No dynamic switch while running.

**Q: Relationship to doorbell project?**

A: Standalone tuning tool — no doorbell business (BLE provisioning, APK protocol, etc.). Uses SDK `h264e_stream_session` and `isp_frame_session` components.
