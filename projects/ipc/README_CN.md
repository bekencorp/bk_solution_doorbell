# Doorbell 项目（BK7259 SMP 解决方案）

* [English](./README.md)

## 1 项目概述

本工程基于 BK7259 SMP 双核架构，移植自 `bk_avdk_smp_dev_7259v2_bringup_25W4801/projects/multimedia/doorbell`，
保持完整的可视门铃功能：UVC/MIPI 摄像头采集、H.264 编码、Wi-Fi 实时音视频传输、BLE 配网、双向音频、ASR 唤醒等。

CPU 分工：

- **CP**：仅完成 `bk_init` 与 `bk_start_ap_system()`，把控制权交给 AP。
- **AP**：运行所有业务（media_service、devices_mgmt、smart_lock 的 doorbell_core/boarding/network、ASR 等）。

## 2 主要功能

| 模块 | 说明 |
| --- | --- |
| 摄像头 | UVC + MIPI ISP（默认 1920x1080@25fps，NV12） |
| 显示 | MIPI DSI（默认 `lcd_device_hx8399c_mipi_1080x1920`），DPU 视频通道 ARGB8888 |
| GPU | VG-Lite Flexa 旋转 / 格式转换 / 压缩 |
| 编解码 | H.264 硬编码（doorbell），软/硬 JPEG 解码 |
| 网络 | LAN UDP/TCP、CS2 P2P、Wi-Fi 实时图传 |
| 蓝牙 | BLE Boarding 配网，BLE Slave-only |
| 语音 | ADK + AEC v3 + G.711/G.722，板载 Mic/Speaker，UVC UAC |
| ASR | KWS + TFLite-Micro + NPU |

详见 `ap/config/bk7259_ap/defconfig`。

## 3 编译

工程依赖 AVDK 源码：`bk_avdk_smp_dev_7259v2_bringup_25W4801`。该 SDK 必须与本 solution 处于同一上级目录，
或通过环境变量 `SDK_DIR` 显式指定。Solution 模式下推荐使用 `dbuild.sh` 包装器（自动注入 `BK_SOLUTION_MODE=1`、`SOLUTION_DIR`、`PROJECT_DIR`）：

```bash
cd projects/doorbell
./dbuild.sh make bk7259 PROJECT=doorbell
```

如果显式指定 SDK 路径：

```bash
SDK_DIR=/abs/path/to/bk_avdk_smp_dev_7259v2_bringup_25W4801 ./dbuild.sh make bk7259 PROJECT=doorbell
```

编译产物位于：

```
projects/doorbell/build/bk7259/doorbell/package/all-app.bin
```

将该 bin 烧录到 BK7259v2 即可。

## 4 烧录与演示

1. 从博通集成官网下载 IOT APK：<https://dl.bekencorp.com/apk/BekenIot.apk>。
2. 注册账号并登录。
3. 添加设备 -> 选择 `可视门铃`。
4. 选择非 5G Wi-Fi，进入蓝牙配网页面。
5. 在扫描到的设备中点击 IP 匹配的项，自动完成配网。
6. 配网完成后会自动开启摄像头与 H.264 图传（分辨率 864x480）。
7. 板载 LCD 与音频通过 APK 上的按钮控制开关。

> 使用前请确认 UVC 摄像头、MIPI LCD、Speaker、Mic 已正确接入开发板。

## 5 工程目录

```
projects/doorbell
├── ap/                       # AP 业务（doorbell + media + ASR）
│   ├── ap_main.c
│   ├── CMakeLists.txt
│   └── config/bk7259_ap/defconfig
├── cp/                       # CP 引导（仅启动 AP）
│   ├── cp_main.c
│   ├── CMakeLists.txt
│   └── config/bk7259/defconfig
├── partitions/bk7259/        # Flash 分区与 RAM 区域
│   ├── auto_partitions.csv
│   └── ram_regions.csv
├── CMakeLists.txt            # 顶层，注入 ../../components 作为 EXTRA_COMPONENTS_DIRS
├── Makefile
├── dbuild.sh / dbuild.ps1    # solution 模式 docker 构建包装
└── .ci                       # CI 编译命令
```

依赖的 solution 公共组件位于 `bk_solution_doorbell_dev_7259v2_bringup_25W4801/components/`：

- `multimedia_device_service`：camera/display/gpu/codec/uvc 设备管理。
- `smart_lock`：doorbell_core、ble_boarding、network_transfer、audio_device、cmd 等业务。
- `lcd_device`：solution 自带 RGB ST7701SN 屏驱动（按需启用）。
