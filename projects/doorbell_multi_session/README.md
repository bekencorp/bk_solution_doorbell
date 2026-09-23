# Doorbell_multi_session Project (BK7259 SMP CS2 Multi-Phone Streaming)

* [中文](./README_CN.md)

## 1 Overview

This project is the **CS2 multi-phone multimedia** variant of the doorbell solution. It is derived from `doorbell`.

Multimedia capabilities match `doorbell`, including camera, LCD, audio, streaming, and BLE provisioning. See the [doorbell project documentation](../doorbell/index.html) for the shared multimedia behavior.

> Goal: multiple phones can watch and control the same device stream at the same time. Gerrit topic: `BK7236SW-13957`.

## 2 Features

| Feature | Description |
| --- | --- |
| Multimedia capability | Same as doorbell: camera, LCD, audio, streaming, and BLE provisioning |
| CS2 multi-session | Device accepts multiple PPCS sessions; default maximum is 4 |
| Per-session control | Camera / audio / LCD are voted per phone session; resources are released only when the last session turns them off |
| Audio talk | Speaker ownership is first-come-first-served so phones do not fight for talkback |

### 2.1 vs doorbell

| Item | doorbell | doorbell_multi_session |
| --- | --- | --- |
| Multi-phone streaming | no, one global media owner | yes, multiple phones can watch/control |
| CS2 session | single session | multi-session (`CONFIG_CS2_MAX_NUM_CONNECTIONS`) |
| Media on/off | any phone changes global resources | per-session votes |
| Config difference | standard doorbell | enables `CONFIG_DOORBELL_MULTI_SESSION` |

Required SDK: `3_0908_59_sdk_rlv_401_multi_client` (CS2 multi-session).

## 3 Quick Start

### 3.1 Hardware

Same as doorbell: BK7259 board, MIPI LCD, MIPI camera, UVC camera, speaker/mic. This project does not support DVP cameras.

### 3.2 Build

```bash
cd projects/doorbell_multi_session
SDK_DIR=/abs/path/to/avdk_sdk ./dbuild.sh make bk7259 PROJECT=doorbell_multi_session
```

Output: `projects/doorbell_multi_session/build/bk7259/doorbell_multi_session/package/all-app.bin`

### 3.3 Demo

1. Provision and start streaming with BekenIot APK (same as doorbell)
2. Sign in on a second and third phone with the same account and add the same device
3. Each phone can turn camera / audio / LCD on or off independently
4. The device releases a media resource only after every phone has turned it off

## 4 Layout

```text
projects/doorbell_multi_session
├── ap/
│   ├── ap_main.c                 # same board config as doorbell
│   ├── audio_param/
│   └── config/bk7259_ap/defconfig
├── cp/
│   ├── cp_main.c
│   └── config/bk7259/defconfig
├── partitions/bk7259/
├── CMakeLists.txt
├── Makefile
└── dbuild.sh
```

Multi-client logic lives in shared solution components, not in this project tree:

- `components/smart_lock/src/doorbell_multi_session_mgr.c`: per-session camera/audio/LCD votes
- `components/smart_lock/src/doorbell_cmd.c`: session-aware command replies
- SDK `bk_network_transfer`: CS2 multi-session send/receive and subscribe

## 5 FAQ

**Q: How many phones can watch at the same time?**

A: Controlled by `CONFIG_CS2_MAX_NUM_CONNECTIONS`. This project defaults to 4. Use 2 or 3 on memory-constrained products.

**Q: Why does the doorbell project still look single-client?**

A: doorbell does not enable `CONFIG_DOORBELL_MULTI_SESSION`, so it keeps the original global media ownership.

**Q: What happens if several phones start talkback together?**

A: The first phone that turns audio on owns the speaker. Other phones can still subscribe to downlink audio.
