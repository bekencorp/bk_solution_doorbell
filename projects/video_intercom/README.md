# video_intercom Project (BK7259 SMP Two-Way Video Intercom)

* [中文](./README_CN.md)

## 1 Overview

This project is the **two-way video intercom** variant of the doorbell solution. Compared with the standard `doorbell`, it replaces the binary command channel with a **JSON-RPC control channel**, and displays both the **remote downlink video (main picture)** and the **local self-view (PIP inset)** on the LCD, together with full-duplex audio — i.e. a "video call".

**Core idea:**

- **Uplink**: local MIPI camera → ISP → H.264 encode → sent over network to the APP/peer.
- **Downlink**: remote H.264 video received from network → hardware decode → GPU composite → LCD; remote audio → speaker.
- **PIP**: the ISP SP channel produces a local self-view inset, overlaid on the top-right of the downlink main picture (WeChat-style video call).
- **JSON-RPC control**: the APP controls camera/audio/LCD/imageStream on/off and parameters via JSON-RPC 2.0.

The feature lives in the `components/smart_intercom` component, which is mutually exclusive with the `smart_lock` (binary control channel) used by standard `doorbell`. The underlying multimedia capabilities (camera, LCD, audio, streaming, BLE provisioning, CS2 P2P, etc.) match `doorbell` — see the [doorbell project documentation](../doorbell/index.html).

CPU split:

- **AP** (M55): runs all multimedia and business logic — ISP/encode/decode, GPU composite, LCD, full-duplex audio, BLE provisioning, the `doorbell_core` state machine, the JSON-RPC control plane, downlink decode and PIP compositor.
- **CP** (M52): after `bk_init()`, runs `db_ipc_msg_init()` for IPC and `pl_wakeup_host()` to power up AP; provides low-power keepalive (keepalive / db_pack / powerctrl) and does **not** call `bk_start_ap_system()` (same as `doorbell_lp`'s CP).

## 2 Features

| Feature | Description |
| --- | --- |
| Two-way video intercom | uplink local-camera H.264 stream + downlink remote H.264 decode/display + full-duplex audio |
| JSON-RPC control plane | JSON-RPC 2.0 method-table dispatch for camera/audio/lcd/imageStream/service |
| Downlink video decode | network H.264 → hardware decode → HSRAM FLEXA ring → GPU scale/rotate → LCD |
| PIP inset | ISP SP self-view (640×360) overlaid top-right of the downlink main picture |
| Downlink zero-copy | `CONFIG_SMART_INTERCOM_DL_ZEROCOPY`: network fragments reassembled directly into decode slots, saving one copy |
| Low-power keepalive | reuses the CP keepalive path (AP power-down + TCP heartbeat) to lower idle power |
| Self-test harness | `db_selftest` CLI: loops the local uplink encoded bitstream back into the downlink decode/display without an APK |

### 2.1 vs standard doorbell

| Item | doorbell | video_intercom |
| --- | --- | --- |
| Control channel | binary commands (`smart_lock`) | JSON-RPC 2.0 (`smart_intercom`) |
| Downlink video | none | remote H.264 decode + GPU composite/display |
| LCD picture | single local preview | downlink main picture + local PIP inset |
| ISP channels | MP only | MP (uplink encode) + SP (PIP self-view) |
| Component | `smart_lock` | `smart_intercom` (`depends on !SMART_INTERCOM` forces smart_lock off) |
| Key defconfig | standard doorbell | `CONFIG_SMART_INTERCOM*`, `CONFIG_NTWK_CTRL_CHAN_JSON`, H.264 QUALITY preset |

## 3 Quick Start

### 3.1 Hardware

- BK7259 board
- MIPI camera (GC2053, 1920×1080@15fps)
- MIPI LCD (HX8399C, 1080×1920)
- Speaker / Mic (full-duplex audio)

### 3.2 Build

```bash
cd projects/video_intercom
SDK_DIR=/abs/path/to/bk_avdk_smp_release_4.0.1 ./dbuild.sh make bk7259 PROJECT=video_intercom
```

Output: `projects/video_intercom/build/bk7259/video_intercom/package/all-app.bin`

### 3.3 Key config options

The options in `ap/config/bk7259_ap/defconfig` that distinguish it from standard doorbell:

```ini
CONFIG_INTEGRATION_DOORBELL=y
CONFIG_SMART_INTERCOM=y                 # enable the smart_intercom component
CONFIG_SMART_INTERCOM_AUTO_TEST=y       # compile the db_selftest self-loopback harness
CONFIG_SMART_INTERCOM_DL_ZEROCOPY=y     # downlink video zero-copy
CONFIG_NTWK_CLIENT_SERVICE_ENABLE=y
CONFIG_NTWK_CTRL_CHAN_JSON=y            # control channel uses JSON
CONFIG_NTWK_CTRL_JSON_RX_MAX_SIZE=8192
CONFIG_CJSON_USE=y
CONFIG_H264_QP_PRESET_QUALITY=y         # 2 Mbps quality preset
```

### 3.4 Demo

1. Provision over BLE with the BekenIot APK and connect (same as doorbell).
2. The APP turns on LCD, camera and audio via JSON-RPC and sends the downlink imageStream receive config.
3. The device uplinks the local camera while decoding and displaying the remote downlink video, with the local self-view as a PIP inset in the top-right corner.
4. Full-duplex audio connects — a video intercom session.

### 3.5 Self-test (no APK)

Use the `db_selftest` CLI to loop the uplink encoded bitstream back into the downlink decode/display, to quickly validate the decode/composite/PIP path:

```text
db_selftest help                 # help
db_selftest lcd on               # turn on LCD
db_selftest cam on               # turn on camera (uplink encode)
db_selftest downlink on [loops]  # arm uplink->downlink loopback (loops=0 = forever)
db_selftest downlink off         # disarm loopback and stop downlink pipeline
db_selftest rpc <preset>         # inject preset JSON-RPC (ping/getconfig/camstatus...)
db_selftest rpc raw {json}       # inject custom JSON-RPC
```

## 4 Layout

```
projects/video_intercom
├── ap/
│   ├── ap_main.c                 # AP board config (MIPI camera/LCD/GPU) + doorbell init + selftest CLI
│   ├── audio_param/              # audio params
│   └── config/bk7259_ap/defconfig
├── cp/
│   ├── cp_main.c                 # CP: IPC init + pl_wakeup_host powers up AP
│   ├── db_ipc_msg/               # AP↔CP IPC routing
│   ├── db_pack/                  # keepalive framing protocol
│   ├── keepalive/                # CP keepalive (TCP heartbeat + AP power-down)
│   └── powerctrl/                # pl_wakeup_host / pl_power_down_host
├── partitions/bk7259/
├── CMakeLists.txt
├── Makefile
└── dbuild.sh
```

The intercom business code lives in `components/smart_intercom/`:

```
components/smart_intercom
├── include/                      # public headers
├── src/
│   ├── doorbell_network_transfer.c   # register ctrl/video/audio recv callbacks
│   ├── doorbell_config.c             # WiFi/Netif/BLE events and provisioning
│   ├── doorbell_cmd.c                # heartbeat/power-on notify, MM status vote
│   ├── doorbell_devices.c            # uplink path: ISP→H.264→network send
│   ├── doorbell_jsonrpc.c            # JSON-RPC 2.0 engine and dispatch
│   ├── doorbell_rpc_service.c        # service.setType (switch TCP/UDP service)
│   ├── doorbell_rpc_camera.c         # camera.turnOn/turnOff/getStatus
│   ├── doorbell_rpc_audio.c          # audio.turnOn/turnOff/getStatus/setAcoustics
│   ├── doorbell_rpc_lcd.c            # lcd.turnOn/turnOff/getStatus
│   ├── doorbell_rpc_image.c          # imageStream.setReceiveConfig (downlink decode)
│   ├── doorbell_rpc_misc.c           # misc.ping
│   ├── doorbell_rpc_solution.c       # solution.getConfig (capability advertise)
│   ├── doorbell_downlink_img_manager.c # downlink H.264 decode slot pool
│   ├── doorbell_downlink_video.c     # downlink decode orchestrator (incl. zero-copy)
│   ├── doorbell_display_compositor.c # PIP compositor (main picture + self-view)
│   ├── doorbell_isp_sp.c             # ISP SP channel (self-view)
│   └── doorbell_keepalive.c          # AP-side keepalive trigger
└── auto_test/
    └── doorbell_selftest.c           # db_selftest self-loopback harness
```

## 5 Implementation

### 5.1 Architecture

```
┌──────────────────────────────────────────────────────────────┐
│  AP (M55)                                                      │
│  Uplink  : MIPI camera → ISP MP(256×144) → H.264 → network    │
│  Downlink: network H.264 → slot → decode → FLEXA ring → GPU → LCD │
│  PIP     : ISP SP(640×360) self-view → GPU blit top-right      │
│  Control : JSON-RPC engine → camera/audio/lcd/imageStream fns  │
│  Audio   : full-duplex (doorbell_common audio_device)         │
└───────────────────────────┬──────────────────────────────────┘
                            │ IPC (db_ipc_msg)
┌───────────────────────────▼──────────────────────────────────┐
│  CP (M52)                                                      │
│  db_ipc_msg / keepalive (TCP heartbeat) / db_pack / powerctrl  │
└──────────────────────────────────────────────────────────────┘
```

### 5.2 Uplink (local → network)

```
MIPI GC2053 1080p@15
    ↓
ISP MP 256×144 NV12 (flexa channel)
    ↓
H.264 encoder (flexa bond)
    ↓
doorbell_devices_task_entry
    ↓
ntwk_trans_video_send() → APP/peer
```

Triggered by JSON-RPC `doorbell.camera.turnOn` (with a stream config).

> **Why 256×144**: this is the seam-free HSRAM-safe sweet spot. 256×144 is exactly 9×16 lines, so a **full-frame** FLEXA ring (`DL_SEG_NUM=9`) is only ~55KB and fits HSRAM alongside the GPU 128KB composite buffer + uplink encode + PIP. Larger sizes (e.g. 512×288) need a ~221KB full-frame ring that does not fit, forcing a shallow ring whose mid-picture wrap de-syncs the h264d→GPU FLEXA bond and raises `VCDEC_DEC_INT_ERROR`. Both dimensions must be multiples of 16.

### 5.3 Downlink (network → local display)

```
APP/peer H.264 AU
    ↓
video channel (zero-copy: reassemble directly into slot; else memcpy)
    ↓
doorbell_downlink_img_manager ready queue
    ↓
db_h264d decode task → bk_h264_decode_frame → HSRAM FLEXA ring
    ↓
h264d→GPU bond → compositor main picture (scale 256×144 → 1080p, rotate 90°)
    ↓
LCD flush (app_mipi_lcd_flush)
```

Triggered by JSON-RPC `doorbell.imageStream.setReceiveConfig` (LCD must be on first). Before entering downlink display, `doorbell_devices_preview_gpu_detach()` releases the single-view preview GPU/HSRAM so the compositor can own the GPU.

**Audio downlink**: `doorbell_bk_net_audio_recv()` → `doorbell_audio_data_callback()` → speaker playback (G.711/G.722/PCM per turnOn params).

### 5.4 LCD composition (PIP)

Layers while downlink is active:

| Layer | Source | Size | Placement |
| --- | --- | --- | --- |
| Main | decoded remote H.264 | 256×144 → full screen | full screen |
| PIP inset | ISP SP self-view | 640×360 NV12 | top-right, 32px margin, 90° blit rotation |

Uplink encode keeps running during downlink display; only the preview GPU is detached.

### 5.5 Downlink zero-copy (`CONFIG_SMART_INTERCOM_DL_ZEROCOPY`)

By default the network reassembly layer copies each H.264 AU from the reassembly buffer into a downlink decode slot (one `os_memcpy`). With zero-copy on, `doorbell_downlink_video.c` registers unfragment callbacks with the network layer, exposing the decode slots as `frame_buffer_t` so fragments are reassembled **directly into decode slots**, saving that copy:

| Callback | Function | Role |
| --- | --- | --- |
| `malloc_cb` | `doorbell_downlink_slot_fb_alloc` | hand out a free slot as `frame_buffer_t` |
| `send_cb` | `doorbell_downlink_slot_fb_commit` | AU already in slot; set length and push to ready queue |
| `free_cb` | `doorbell_downlink_slot_fb_release` | return slot to free stack |

If registration fails (e.g. path not ready) it degrades gracefully to the copy path. Slots must exist before registration (`doorbell_downlink_img_manager_init` at downlink start).

### 5.6 JSON-RPC control plane

**Ingress:**

```
peer APP → network control channel (JSON, CONFIG_NTWK_CTRL_CHAN_JSON)
    ↓
doorbell_bk_net_cntrl_recv()
    ↓
doorbell_jsonrpc_handle_cmd()  → cJSON_Parse → method string
    ↓
method table dispatch (14 entries) → doorbell_rpc_*.fn(params, id)
```

**Method table:**

| Method | Description |
| --- | --- |
| `doorbell.service.setType` | switch LAN UDP/TCP service |
| `doorbell.camera.turnOn` / `turnOff` / `getStatus` | uplink camera on/off/status |
| `doorbell.audio.turnOn` / `turnOff` / `getStatus` / `setAcoustics` | full-duplex audio on/off/status/AEC params |
| `doorbell.lcd.turnOn` / `turnOff` / `getStatus` | LCD on/off/status |
| `doorbell.imageStream.setReceiveConfig` | start downlink H.264 decode/display |
| `doorbell.misc.ping` | liveness probe |
| `doorbell.solution.getConfig` | advertise device capabilities |

**Egress:** handlers call `doorbell_rpc_send_result/error` → `ntwk_trans_ctrl_send()`. The device also sends `doorbell.notify.heartbeat` (timer) and `doorbell.notify.powerOn` (on connect) notifications.

## 6 API Reference (`components/smart_intercom/include/`)

**JSON-RPC** (`doorbell_jsonrpc.h`):

```c
int  doorbell_jsonrpc_handle_cmd(const char *json, int length);
void doorbell_jsonrpc_send_notify(const char *method, cJSON *params);
```

**Downlink video** (`doorbell_downlink_video.h`):

```c
bk_err_t doorbell_downlink_set_h264_receive_config(const doorbell_dl_recv_cfg_t *cfg);
bk_err_t doorbell_downlink_video_recv(const uint8_t *data, uint32_t length);
bk_err_t doorbell_downlink_video_stop(void);
bool     doorbell_downlink_video_is_running(void);
```

**PIP compositor** (`doorbell_display_compositor.h`):

```c
bk_err_t doorbell_compositor_start(const doorbell_comp_cfg_t *cfg, void *flexa_ring, uint32_t flexa_buf_count);
bk_err_t doorbell_compositor_stop(void);
bool     doorbell_compositor_is_running(void);
```

**ISP SP self-view** (`doorbell_isp_sp.h`):

```c
bk_err_t doorbell_isp_sp_open(uint16_t width, uint16_t height);
bk_err_t doorbell_isp_sp_read_nv12(frame_buffer_t *frame, uint32_t size, uint32_t timeout_ms);
bk_err_t doorbell_isp_sp_close(void);
```

**Intercom device extensions** (`doorbell_devices_intercom.h`):

```c
void *doorbell_devices_isp_handle_get(void);
void  doorbell_devices_preview_gpu_detach(void);
void  doorbell_devices_preview_gpu_attach(void);
void  doorbell_devices_force_idr(void);
```

**Self-test** (`auto_test/doorbell_selftest.h`):

```c
int  doorbell_selftest_cli_init(void);
void doorbell_selftest_downlink_tee_feed(const uint8_t *data, uint32_t len); /* no-op when AUTO_TEST off */
```

## 7 FAQ

**Q: Can video_intercom and doorbell be built into the same image?**

A: No. The `smart_intercom` Kconfig makes `smart_lock` satisfy `depends on !SMART_INTERCOM` and be disabled — they are mutually exclusive. With `CONFIG_SMART_INTERCOM=y`, `smart_lock` compiles to an empty library.

**Q: Why is the downlink video size fixed at 256×144? Can it be larger?**

A: It is limited by HSRAM size. 256×144 is the HSRAM-safe size that fits a full-frame FLEXA ring; larger sizes cause ring wrap, h264d→GPU de-sync, and `VCDEC_DEC_INT_ERROR`. Both dimensions must be multiples of 16.

**Q: How to validate downlink decode and PIP without an APP?**

A: Use `db_selftest downlink on` to loop the local uplink encoded bitstream back into the downlink decode/display (requires `CONFIG_SMART_INTERCOM_AUTO_TEST=y`).

**Q: What protocol does the control channel use?**

A: JSON-RPC 2.0 (`CONFIG_NTWK_CTRL_CHAN_JSON=y`), unlike the binary command channel of standard doorbell.
