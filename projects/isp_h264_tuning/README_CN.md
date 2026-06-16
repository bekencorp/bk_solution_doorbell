# Media Preview 联调工程

## 1. 概述

本工程提供统一的 PC 预览服务，H264E 码流与 ISP 抓帧共用一套 `network_transfer`：

- PC 通过 JSON-RPC method 选择 H264E 或 ISP 模块。
- CTRL 通道：TCP 7100，JSON-RPC。
- VIDEO 通道：TCP 7150，H264 ES 或 ISP NV12 帧。

组件：

- `h264e_stream`：H264 编码会话与 `h264EScream.*` RPC。
- `isp_frame`：ISP 抓帧会话与 `ispFrame.*` RPC。
- `media_preview_server`：统一网络服务与 RPC 分发。

## 2. 目录结构

```text
projects/media_preview_example/
├── ap/
│   ├── ap_main.c
│   ├── media_preview_server.c
│   ├── h264e_stream_project.c
│   ├── isp_frame_project.c
│   └── config/bk7259_ap/
├── cp/
│   ├── cp_main.c
│   └── config/bk7259/
└── partitions/bk7259/
```

## 3. 编译

```bash
cd projects/media_preview_example
./dbuild.sh make bk7259
```

## 4. 启动

上电后自动：

1. 连接 STA WiFi（`Beken-ACL-2.4G`）。
2. 启动 `media_preview` 统一预览服务（默认 1080p 25fps）。

## 5. CLI

```text
ap_cmd media_preview start 1080p 25
ap_cmd media_preview stop
ap_cmd h264e_stream server start 1080p 25
ap_cmd isp_frame server start 1080p 25
```

## 6. JSON-RPC 示例

H264E：

```json
{"jsonrpc":"2.0","method":"h264EScream.getRateControl","params":{},"id":1}
```

ISP：

```json
{"jsonrpc":"2.0","method":"ispFrame.start","params":{},"id":1}
```

```json
{"jsonrpc":"2.0","method":"ispFrame.stop","params":{},"id":2}
```
