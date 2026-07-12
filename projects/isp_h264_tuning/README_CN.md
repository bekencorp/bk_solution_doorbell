# ISP / H264E 联调工程

* [English](./README.md)

## 1. 概述

本工程（`isp_h264_tuning`）是面向 **PC 端图像质量联调** 的独立工程，通过 Wi-Fi 提供统一的预览服务，支持 ISP 原始帧抓图和 H264E 编码码流两种模式。

**典型用途**：

- ISP 图像质量调参（AE/AWB/降噪等），PC 端实时预览 NV12 帧
- H264E 编码参数调优（码率、QP 等），PC 端实时预览 H.264 ES 码流
- 验证 Flexa 零拷贝 ISP→H264E 编码链路

### 1.1 网络架构

PC 与开发板通过 Wi-Fi 通信，共用一套 `network_transfer` 实例：

| 通道 | 端口 | 协议 | 用途 |
| --- | --- | --- | --- |
| CTRL | TCP 7100 | JSON-RPC 2.0 | 参数查询/设置、启停控制 |
| VIDEO | TCP 7150 | 二进制流 | H264 ES 或 ISP NV12 帧 |

PC 通过 JSON-RPC 的 `method` 字段选择 H264E 或 ISP 模块；同一时刻只有一个模块处于 active 状态。

### 1.2 软件组件

| 组件 | 源文件 | 职责 |
| --- | --- | --- |
| `media_preview_server` | `ap/media_preview_server.c` | 统一网络服务、RPC 分发、模块切换 |
| `h264e_stream_project` | `ap/h264e_stream_project.c` | H264 编码会话、`h264EScream.*` RPC |
| `isp_frame_project` | `ap/isp_frame_project.c` | ISP 抓帧会话、`ispFrame.*` RPC |

## 2. 默认参数

| 项 | 默认值 | 说明 |
| --- | --- | --- |
| 分辨率 | 2304×1296 | 可通过 CLI / RPC 切换 |
| 帧率 | 20 fps | |
| H264E 码率 | 1200 kbps | `setRateControl` 可改 |
| 传感器 | GC2053 / CV2005 / SC3336 | defconfig 均已启用 |
| Wi-Fi STA | 上电自动连接 | SSID/密码在 `ap_main.c` 中配置 |
| BLE / USB | 关闭 | 纯 Wi-Fi 联调工程 |

支持分辨率：**2304×1296**、**1920×1080**、**1280×720**（CLI 支持 `1080p` / `720p` 简写）。

### 2.1 默认板级硬件配置

MIPI 引脚（与 doorbell 相同，见 `h264e_stream_project.c` / `isp_frame_project.c`）：

| 信号 | GPIO |
| --- | --- |
| I2C SCL / SDA | GPIO_69 / GPIO_70 |
| Sensor Reset / XCLK | GPIO_71 / GPIO_59 |

### 2.2 H264E vs ISP 链路差异

| 对比项 | H264E 模式 | ISP 模式 |
| --- | --- | --- |
| ISP 工作模式 | Flexa（零拷贝） | Frame（非 Flexa） |
| 下游 | H264E encoder（Flexa bond） | 直接输出 NV12 帧 |
| RPC 前缀 | `h264EScream.*` / `h264e.*` | `ispFrame.*` / `isp.*` |
| VIDEO 通道数据 | H.264 ES 码流 | NV12 原始帧 |
| 典型用途 | 编码质量/码率调参 | ISP 图像质量调参 |

## 3. 目录结构

```text
projects/isp_h264_tuning/
├── ap/
│   ├── ap_main.c                 # Wi-Fi 自动连接 + 服务自启动
│   ├── media_preview_server.c    # 统一 RPC 分发
│   ├── h264e_stream_project.c    # H264E 模块
│   ├── isp_frame_project.c       # ISP 抓帧模块
│   └── config/bk7259_ap/defconfig
├── cp/
│   ├── cp_main.c                 # 仅引导 AP
│   └── config/bk7259/defconfig
├── partitions/bk7259/
├── CMakeLists.txt
├── Makefile
└── dbuild.sh
```

## 4. 编译

```bash
cd projects/isp_h264_tuning
SDK_DIR=/abs/path/to/avdk_sdk ./dbuild.sh make bk7259 PROJECT=isp_h264_tuning
```

产物：

```
projects/isp_h264_tuning/build/bk7259/isp_h264_tuning/package/all-app.bin
```

## 5. 启动与 PC 连接

### 5.1 上电自动流程

1. `bk_init()` → `media_service_init()` → `devices_mgmt_init()`
2. 注册 CLI（h264e / isp / media_preview）
3. 自动连接 STA WiFi（参数见 `ap_main.c`）
4. 自动启动 media_preview_server（2304×1296，20fps）

### 5.2 串口日志

```
media preview example ready.
  Unified server : default 2304x1296 @ 20fps
  PC selects H264E or ISP by JSON-RPC method
  CTRL:  TCP 7100 JSON-RPC (H264E or ISP methods)
  VIDEO: TCP 7150 H264 ES or ISP frames
  STA IP: x.x.x.x ...
```

### 5.3 PC 连接步骤

1. 确保 PC 与开发板在同一 Wi-Fi 网络
2. 从串口日志获取板端 IP
3. PC 工具连接 `board_ip:7100`（CTRL）与 `board_ip:7150`（VIDEO）
4. 通过 JSON-RPC 选择模块并控制启停

## 6. CLI 命令

```text
# 统一预览服务
ap_cmd media_preview start 2304x1296 20
ap_cmd media_preview start 1080p 25          # 简写分辨率
ap_cmd media_preview stop

# 单独模块（不经过统一 server 时使用）
ap_cmd h264e_stream server start 2304x1296 20
ap_cmd h264e_stream server stop
ap_cmd isp_frame server start 2304x1296 20
ap_cmd isp_frame server stop
```

## 7. JSON-RPC 参考

### 7.1 通用控制

```json
{"jsonrpc":"2.0","method":"media.start","params":{},"id":1}
{"jsonrpc":"2.0","method":"media.stop","params":{},"id":2}
```

### 7.2 H264E 模式

查询码率控制：

```json
{"jsonrpc":"2.0","method":"h264EScream.getRateControl","params":{},"id":1}
```

设置码率与 QP：

```json
{"jsonrpc":"2.0","method":"h264EScream.setRateControl","params":{"rateCtrl":{"bitrate":1200000,"qpMinI":18,"qpMaxI":40,"qpMinP":22,"qpMaxP":44}},"id":2}
```

其他支持的 method 前缀：`h264e.*`、`doorbell.encoder.*`、`encode_preview.*`、`start_encode`、`stop_encode`、`force_idr`、`get_rate_ctrl`、`set_rate_ctrl`。

### 7.3 ISP 模式

```json
{"jsonrpc":"2.0","method":"ispFrame.start","params":{},"id":1}
{"jsonrpc":"2.0","method":"ispFrame.stop","params":{},"id":2}
{"jsonrpc":"2.0","method":"ispFrame.getConfig","params":{},"id":3}
{"jsonrpc":"2.0","method":"ispFrame.setConfig","params":{},"id":4}
{"jsonrpc":"2.0","method":"ispFrame.captureFrame","params":{},"id":5}
```

其他支持的 method 前缀：`isp.*`、`isp_capture.*`、`doorbell.isp.*`。

### 7.4 模块切换规则

- PC 发送 H264E 类 method → 自动激活 H264E 模块，停止 ISP 模块
- PC 发送 ISP 类 method → 自动激活 ISP 模块，停止 H264E 模块
- 发送 stop method（如 `stop_encode`、`ispFrame.stop`）→ 释放当前模块
- 若 method 与当前 active 模块不匹配，返回 JSON-RPC error `-32003`

## 8. 关键 defconfig

| 配置项 | 说明 |
| --- | --- |
| `CONFIG_H264E_STREAM_SESSION` | H264E 编码会话 |
| `CONFIG_ISP_FRAME_SESSION` | ISP 抓帧会话 |
| `CONFIG_NTWK_CTRL_CHAN_JSON` | JSON-RPC 控制通道 |
| `CONFIG_ENCODER_H264_CLK_240M` | H264E 时钟 240MHz |
| `CONFIG_CJSON_USE` | cJSON 解析 |
| `# CONFIG_BLUETOOTH_AP` | BLE 关闭 |
| `# CONFIG_USB` | USB 关闭 |

## 9. 常见问题

**Q: Wi-Fi 连接失败？**

A: 在 `ap_main.c` 中修改 `MEDIA_PREVIEW_STA_SSID` / `MEDIA_PREVIEW_STA_PASSWORD` 后重新编译，或通过 SDK Wi-Fi CLI 手动连接。

**Q: PC 连上 7100 但 7150 无数据？**

A: 需先通过 JSON-RPC 发送 start method（如 `h264EScream.turnOn` 或 `ispFrame.start`）启动对应模块的 media pipeline。

**Q: 如何切换分辨率？**

A: CLI 启动时指定（如 `media_preview start 1080p 25`），或在 stop 后重新 start。运行中不支持动态切换。

**Q: 与 doorbell 工程的关系？**

A: 本工程是独立的联调工具，不包含 doorbell 业务（BLE 配网、APK 协议等）。共用 SDK 中的 `h264e_stream_session` 和 `isp_frame_session` 组件。
