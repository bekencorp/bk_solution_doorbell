# Doorbell_multi_session 项目（BK7259 SMP CS2 多路图传方案）

* [English](./README.md)

## 1 项目概述

本项目是 doorbell 解决方案中的 **CS2 多路多媒体传输方案**，由 `doorbell` 派生而来。

多媒体能力（摄像头、LCD、音频、图传、BLE 配网等）与 `doorbell` 一致，详见 [doorbell 工程文档](../doorbell/index.html)。

> 目标能力：多部手机可同时观看并控制图传。对应 Gerrit topic `BK7236SW-13957`。

## 2 功能特性

| 特性 | 说明 |
| --- | --- |
| 多媒体能力 | 与 doorbell 保持一致，支持摄像头、LCD、音频、图传和 BLE 配网 |
| CS2 多会话 | 设备侧同时接受多路 PPCS session，默认最多 4 路 |
| 按会话控制 | 摄像头 / 音频 / LCD 按手机 session 投票，最后一路关闭才释放资源 |
| 音频对讲 | 扬声器归属由先到先得的 session 持有，避免多路对讲互相抢占 |

### 2.1 与 doorbell 的差异

| 对比项 | doorbell | doorbell_multi_session |
| --- | --- | --- |
| 多路图传 | 不支持，全局只有一路媒体归属 | 支持，多部手机可同时观看/控制 |
| CS2 session | 单会话 | 多会话（`CONFIG_CS2_MAX_NUM_CONNECTIONS`） |
| 媒体开关 | 任一手机开关都会直接影响全局资源 | 按 session 投票，互不影响 |
| 配置差异 | 标准 doorbell | 额外启用 `CONFIG_DOORBELL_MULTI_SESSION` |

依赖 SDK：`3_0908_59_sdk_rlv_401_multi_client`（含 CS2 multi-session）。

## 3 快速开始

### 3.1 硬件准备

与 doorbell 相同：BK7259 开发板、MIPI LCD、MIPI 摄像头、UVC 摄像头、Speaker/Mic。本工程不支持 DVP 摄像头。

### 3.2 编译

```bash
cd projects/doorbell_multi_session
SDK_DIR=/abs/path/to/avdk_sdk ./dbuild.sh make bk7259 PROJECT=doorbell_multi_session
```

产物：`projects/doorbell_multi_session/build/bk7259/doorbell_multi_session/package/all-app.bin`

### 3.3 演示流程

1. 使用 BekenIot APK 配网并开启图传（同 doorbell）
2. 再用第二部、第三部手机登录同一账号，添加同一台设备
3. 各手机可独立打开/关闭摄像头、音频、LCD
4. 仅当所有手机都关闭某路媒体后，设备才真正释放该资源

## 4 工程目录

```text
projects/doorbell_multi_session
├── ap/
│   ├── ap_main.c                 # 与 doorbell 相同的板级配置
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

多路控制实现位于 solution 公共组件，而不是本工程目录：

- `components/smart_lock/src/doorbell_multi_session_mgr.c`：按 session 投票管理摄像头/音频/LCD
- `components/smart_lock/src/doorbell_cmd.c`：命令按 session 回包
- SDK `bk_network_transfer`：CS2 多会话收发与订阅

## 5 常见问题

**Q: 最多支持几部手机同时观看？**

A: 由 `CONFIG_CS2_MAX_NUM_CONNECTIONS` 决定，本工程默认 4。内存紧张时可改为 2 或 3。

**Q: 为什么 doorbell 工程看不到多路能力？**

A: doorbell 未启用 `CONFIG_DOORBELL_MULTI_SESSION`，仍按单路全局媒体归属工作。

**Q: 多部手机同时对讲会怎样？**

A: 先打开音频的手机获得扬声器归属，其他手机仍可订阅下行音频，但不能同时抢占对讲。
