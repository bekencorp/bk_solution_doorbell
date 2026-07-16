# Doorbell 解决方案（BK7259）

## 概述

BK7259 SMP 门铃方案，包含可视门铃、低功耗保活门铃、无屏 IPC 及 ISP/H264E 联调工程。

| 工程 | 说明 |
| --- | --- |
| `doorbell` | 可视门铃：MIPI/UVC、H.264 图传、MIPI LCD、BLE 配网 |
| `doorbell_lp` | 在 doorbell 基础上增加低功耗保活能力 |
| `ipc` | 无屏 IPC：SC3336 2304×1296 |
| `isp_h264_tuning` | ISP / H264E PC 端图像质量联调 |

各工程详细说明见 `projects/<name>/README_CN.md`。

## 编译

Solution 依赖 AVDK SDK（`bk_avdk_smp`）。SDK 需与本 solution 同级，或通过 `SDK_DIR` 指定。

```bash
cd projects/doorbell
./dbuild.sh make bk7259 PROJECT=doorbell
```

显式指定 SDK 路径：

```bash
SDK_DIR=/abs/path/to/bk_avdk_smp ./dbuild.sh make bk7259 PROJECT=doorbell
```

产物路径：`projects/<name>/build/bk7259/<name>/package/all-app.bin`

## 演示

1. 下载 APK：<https://dl.bekencorp.com/apk/BekenIot.apk>
2. 注册登录，添加设备 → `可视门铃`
3. 选择 2.4G Wi-Fi，BLE 配网
4. 配网完成后自动开启摄像头与 H.264 图传（doorbell 默认 MIPI 1920×1080）
5. LCD 与音频通过 APK 按钮控制

> 测试前请确认 MIPI 摄像头、MIPI LCD、Speaker、Mic 已接入；使用 UVC 时需接 USB。

## 文档

Sphinx 文档位于 `docs/bk7259/`。生成 HTML：

```bash
cd docs
make doc
```

输出目录：`docs/build/doc/bk7259/`。
