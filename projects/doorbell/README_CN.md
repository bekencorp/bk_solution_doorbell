# Doorbell 项目（BK7259 SMP 解决方案）

* [English](./README.md)

## 1 项目概述

本工程基于 BK7259 SMP 双核架构，实现完整的可视门铃功能：UVC/MIPI 摄像头采集、H.264 编码、Wi-Fi 实时音视频传输、BLE 配网、双向音频、ASR 唤醒等。

CPU 分工：

- **CP**（M52）：`bk_init()` 初始化 Wi-Fi 协议栈、BLE Controller、射频校准（`vnd_cal`）及 CIF 等与 AP 的通信底层；随后 `bk_start_ap_system()` 启动 AP，并持续运行无线侧控制（Wi-Fi/BLE 共存）。
- **AP**（M55）：运行多媒体与门铃业务（`media_service`、`devices_mgmt`、`smart_lock` 的 doorbell_core/boarding/network、BLE Host、ASR 等）。

## 2 主要功能

| 模块 | 说明 |
| --- | --- |
| 摄像头 | MIPI 摄像头（1920×1080@25fps，NV12，Flexa）+ UVC（defconfig 已启用） |
| 显示 | MIPI DSI（默认 `lcd_device_hx8399c_mipi_1080x1920`），DPU 视频通道 ARGB8888 |
| GPU | VG-Lite Flexa 旋转 90° / 格式转换 / 压缩 |
| 编解码 | H.264 硬编码，软/硬 JPEG 解码 |
| 网络 | LAN UDP/TCP、CS2 P2P、Wi-Fi 实时图传 |
| 蓝牙 | BLE Boarding 配网，BLE Slave-only |
| 语音 | ADK + AEC v3 + G.711/G.722，板载 Mic/Speaker，UVC UAC |
| ASR | KWS + TFLite-Micro + NPU |
| 快照 | SD 卡 JPEG 抓拍（`CONFIG_MDS_SNAPSHOT`） |

默认板级配置见 `ap/ap_main.c`，完整 Kconfig 见 `ap/config/bk7259_ap/defconfig`。

## 3 编译

工程依赖 AVDK 源码 `avdk_sdk`。该 SDK 必须与本 solution 处于同一上级目录，
或通过环境变量 `SDK_DIR` 显式指定。Solution 模式下推荐使用 `dbuild.sh` 包装器（自动注入 `BK_SOLUTION_MODE=1`、`SOLUTION_DIR`、`PROJECT_DIR`）：

```bash
cd projects/doorbell
./dbuild.sh make bk7259 PROJECT=doorbell
```

如果显式指定 SDK 路径：

```bash
SDK_DIR=/abs/path/to/avdk_sdk ./dbuild.sh make bk7259 PROJECT=doorbell
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
6. 配网完成后会自动开启摄像头与 H.264 图传（分辨率 1920x1080）。
7. 板载 LCD 与音频通过 APK 上的按钮控制开关。

使用前请确认 MIPI 摄像头、MIPI LCD、Speaker、Mic 已正确接入开发板；若使用 UVC 摄像头，需接入 USB 接口。doorbell 工程不支持 DVP 摄像头。

## 5 工程目录

```
projects/doorbell
├── ap/                       # AP 业务（doorbell + media + ASR）
│   ├── ap_main.c             # 板级 camera/display/gpu 配置
│   ├── CMakeLists.txt
│   ├── audio_param/
│   └── config/bk7259_ap/defconfig
├── cp/                       # CP（M52）：Wi-Fi/BLE Controller、vnd_cal，bk_start_ap_system() 启动 AP
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

依赖的 solution 公共组件位于Solution下的 `components/`：

- `multimedia_device_service`：camera/display/gpu/codec/uvc 设备管理。
- `smart_lock`：doorbell_core、ble_boarding、network_transfer、audio_device、cmd 等业务。
- `lcd_device`：solution 自带 RGB ST7701SN 屏驱动（按需启用）。
