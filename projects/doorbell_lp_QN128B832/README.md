# Doorbell_lp_QN128B832 Project (BK7259QN128B832 SMP Low-Power Keepalive)

* [中文](./README_CN.md)

## 1 Overview

This project is the **BK7259QN128B832 adaptation** of the **low-power keepalive variant** in the doorbell solution. It is derived from `doorbell_lp` (which adds low-power keepalive on top of `doorbell` after multimedia services become idle).

Multimedia capabilities match `doorbell`, including camera, LCD, audio, streaming, and BLE provisioning. See the [doorbell project documentation](../doorbell/index.html) for the shared multimedia behavior.

> Target chip: **BK7259QN128B832 (B-package)**.

## 2 Features

| Feature | Description |
| --- | --- |
| Multimedia capability | Same as doorbell: camera, LCD, audio, streaming, and BLE provisioning |
| Low-power keepalive | Enters keepalive after camera, audio, LCD, and other multimedia services are turned off |
| Fast recovery | Restores doorbell and multimedia services after a wake request |
| Keepalive CLI | Configures idle detection interval for debug and demos |

### 2.1 vs doorbell

| Item | doorbell | doorbell_lp |
| --- | --- | --- |
| Low-power keepalive | none | enter keepalive after multimedia services are idle |
| Multimedia capability | camera, LCD, audio, streaming, BLE provisioning | same as doorbell |
| ASR auto-start | yes | no (defconfig enabled, call manually) |
| Config difference | standard doorbell | low-power keepalive options enabled |

## 3 Quick Start

### 3.1 Hardware

Same as doorbell: BK7259 board, MIPI LCD, MIPI camera, UVC camera, speaker/mic. The doorbell_lp project does not support DVP cameras.

### 3.2 Build

```bash
cd projects/doorbell_lp_QN128B832
SDK_DIR=/abs/path/to/avdk_sdk ./dbuild.sh make bk7259 PROJECT=doorbell_lp_QN128B832
```

Output: `projects/doorbell_lp_QN128B832/build/bk7259/doorbell_lp_QN128B832/package/all-app.bin`

### 3.3 Demo

1. Run normal BekenIot APK provisioning and streaming (same as doorbell)
2. Turn off all multimedia in APK: camera, audio, and LCD
3. Wait about 10 s (default idle check interval) until the system enters keepalive
4. Send a wake request from the server, then streaming/audio resume

### 3.4 Keepalive CLI

```text
ka                          # show help
ka interval <ms>            # idle check interval (3000-300000 ms, persisted to Flash)
```

## 4 Layout

```text
projects/doorbell_lp_QN128B832
├── ap/
│   ├── ap_main.c                 # doorbell config + keepalive init (B-package camera pin mapping)
│   ├── audio_param/
│   └── config/bk7259_ap/defconfig
├── cp/
│   ├── cp_main.c
│   ├── keepalive/
│   ├── db_ipc_msg/
│   ├── db_pack/
│   └── powerctrl/
├── partitions/bk7259/
├── CMakeLists.txt
├── Makefile
└── dbuild.sh
```

## 5 FAQ

**Q: How do I confirm the power difference after entering keepalive?**

A: Compare current before and after turning off multimedia services with a power meter.

**Q: Can BLE provision while in keepalive?**

A: It is not recommended. Wake the device first, then follow the normal doorbell provisioning flow.

**Q: What does the idle check interval control?**

A: It controls how long the project waits after multimedia services are turned off before entering keepalive. It can be adjusted through the CLI.
