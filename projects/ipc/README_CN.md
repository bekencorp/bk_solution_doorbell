# IPC 项目（BK7259 SMP 解决方案）

* [English](./README.md)

## 1 项目概述

本工程是 doorbell 解决方案中的 **IPC 网络摄像头方案**，基于 BK7259 SMP 双核架构。
与 `doorbell` 共用 `smart_lock` 门铃业务栈（BLE 配网、TCP/UDP 图传、双向音频、ASR），但 **默认不启用 LCD/GPU 显示链路**，
面向无屏 IPC / 网络摄像头产品。

### 1.1 CPU 分工

| 核心 | 职责 |
| --- | --- |
| **CP**（M52） | `bk_init()` 与 `bk_start_ap_system()`（同 doorbell） |
| **AP**（M55） | media_service、devices_mgmt、doorbell 业务、ASR、AT 命令 |

### 1.2 与 doorbell 的定位差异

| 对比项 | doorbell | ipc（本工程） |
| --- | --- | --- |
| 目标场景 | 可视门铃（带屏） | 无屏 IPC 摄像头 |
| 默认传感器 | GC2053 1920×1080@25fps | SC3336 2304×1296@20fps |
| LCD / GPU / DPU | ap_main 启用 | ap_main 未配置（defconfig 保留 LCD/GPU 驱动选项） |
| AT 命令 | 启用（port 5） | 启用（port 5，WiFi/MISC AT） |
| ASR | 自动启动 | 自动启动 |
| UVC | defconfig 启用 | defconfig 启用（ap_main 仅配 MIPI） |
| CS2 P2P | 启用 | 未启用（仅保留 CS2 buffer 配置） |

## 2 主要功能

| 模块 | 说明 |
| --- | --- |
| 摄像头 | MIPI CSI（默认 SC3336，2304×1296@20fps，NV12，Flexa） |
| 编解码 | H.264 硬编码，JPEG 解码 |
| 网络 | LAN UDP/TCP 图传（doorbell 业务栈） |
| 蓝牙 | BLE Boarding 配网 |
| 语音 | ADK + AEC v3 + G.711/G.722，板载 Mic/Speaker，UVC UAC |
| ASR | KWS + TFLite-Micro + NPU（SRAM 模式，`CONFIG_ASR_SERVICE_USE_SRAM`） |
| AT | 默认端口 5，支持 `CONFIG_WIFI_AT_ENABLE`、`CONFIG_MISC_AT_ENABLE` |

### 2.1 默认板级硬件配置

引脚定义见 ap/ap_main.c（与 doorbell 共用同一套 MIPI 引脚）：

| 信号 | GPIO | 说明 |
| --- | --- | --- |
| MIPI I2C SCL | GPIO_69 | bus id = 1 |
| MIPI I2C SDA | GPIO_70 | |
| MIPI Sensor Reset | GPIO_71 | |
| MIPI Sensor XCLK | GPIO_59 | |

**Camera（MIPI CSI）**

- 传感器：SC3336（`CONFIG_CSI_SC3336`）
- 最大分辨率：2304×1296@20fps
- ISP 主通道：2304×1296，NV12，Flexa 开启
- 无 display / gpu 板级初始化

### 2.2 视频数据流

```
SC3336 (MIPI CSI, 2304×1296 NV12 Flexa)
    │
    └──► H.264 Encoder (Flexa bond) ──► Wi-Fi 图传
```

无 LCD 分支。高分辨率采集后直接 H.264 编码输出，图传分辨率与 ISP MP 板级配置一致（2304×1296）。

### 2.3 网络传输

端口与协议同 doorbell（CTRL 7100 / VIDEO 7150 TCP，7180 UDP / AUDIO 7140 TCP，7170 UDP）。
演示时使用 BekenIot APK 配网，配网后自动开启 H.264 图传（2304×1296）。

### 2.4 AT 命令

defconfig 启用 AT 子系统，默认端口 5（`CONFIG_DEFAULT_AT_PORT=5`）。
可用于产测阶段的 Wi-Fi 连接、设备信息查询等自动化测试。

## 3 编译

### 3.1 依赖

AVDK SDK（`avdk_sdk`），通过 `SDK_DIR` 指定。

### 3.2 编译命令

```bash
cd projects/ipc
SDK_DIR=/abs/path/to/avdk_sdk ./dbuild.sh make bk7259 PROJECT=ipc
```

本地编译：

```bash
make bk7259 SDK_DIR=/abs/path/to/avdk_sdk PROJECT=ipc
```

### 3.3 编译产物

```
projects/ipc/build/bk7259/ipc/package/all-app.bin
```

## 4 烧录与演示

### 4.1 硬件准备

- BK7259 开发板
- MIPI CSI 摄像头（默认 SC3336，2304 万像素）
- 板载 Speaker / Mic
- 无需 LCD

### 4.2 演示流程

与 doorbell 相同，使用 BekenIot APK：

1. 下载 APK → 注册登录 → 添加 `可视门铃` 设备
2. BLE 配网（2.4G Wi-Fi）
3. 配网成功后自动开启 H.264 图传
4. 通过 APK 控制音频开关（无 LCD 按钮）

### 4.3 适用场景

- 室内/室外 IPC 摄像头（无本地屏）
- 需要 AT 命令产测的批量设备
- 高分辨率 Sensor（2304×1296）图传验证
- 关键词唤醒（ASR）+ 远程对讲

## 5 工程目录

```
projects/ipc
├── ap/
│   ├── ap_main.c                 # 仅 camera 板级配置
│   ├── audio_param/audio_para.c
│   └── config/bk7259_ap/defconfig
├── cp/
│   ├── cp_main.c
│   └── config/bk7259/defconfig
├── partitions/bk7259/
├── CMakeLists.txt
├── Makefile
├── dbuild.sh / dbuild.ps1
└── .ci
```

### 5.1 关键 defconfig 差异（相对 doorbell）

| 配置项 | ipc | doorbell |
| --- | --- | --- |
| `CONFIG_CSI_SC3336` | y | n |
| `CONFIG_CSI_GC2053` | n | y |
| `CONFIG_APP_GPU` | n | y |
| `CONFIG_CS2_P2P_SERVER` | n | y |
| `CONFIG_ASR_SERVICE_USE_SRAM` | y | n |
| `CONFIG_AT` / `CONFIG_WIFI_AT_ENABLE` | y | y |
| `CONFIG_MDS_SNAPSHOT` | n | y |

公共组件路径：`../../components/`。

## 6 常见问题

**Q: 能否接 LCD？**

A: defconfig 保留了 LCD 驱动选项，但 `ap_main.c` 未配置 display/gpu。如需带屏，建议直接使用 `doorbell` 工程或自行在 ap_main 中添加 display 配置。

**Q: 如何更换传感器？**

A: 修改 `ap/ap_main.c` 中的分辨率/引脚参数，并在 defconfig 中切换 `CONFIG_CSI_*` 传感器选项。

**Q: 图传分辨率是多少？**

A: 与采集一致，为 2304×1296。`app_h264e_turn_on()` 直接读取 ISP MP 通道尺寸；MIPI 路径下 APK `DBCMD_SET_CAMERA_TURN_ON` 中的 width/height 不会改编码分辨率。
