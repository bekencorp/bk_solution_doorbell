# Doorbell_lp_QN128B832 项目（BK7259QN128B832 SMP 低功耗保活方案）

* [English](./README.md)

## 1 项目概述

本项目是 doorbell 解决方案中 **低功耗保活方案** 的 **BK7259QN128B832 适配工程**，由 `doorbell_lp` （在 `doorbell` 基础上增加多媒体空闲后的低功耗保活能力）派生而来。。

多媒体能力（摄像头、LCD、音频、图传、BLE 配网等）与 `doorbell` 一致，详见 [doorbell 工程文档](../doorbell/index.html)。

> 目标芯片：**BK7259QN128B832（B 版封装）**。

## 2 功能特性

| 特性 | 说明 |
| --- | --- |
| 多媒体能力 | 与 doorbell 保持一致，支持摄像头、LCD、音频、图传和 BLE 配网 |
| 低功耗保活 | 关闭摄像头、音频、LCD 等多媒体服务后进入保活状态 |
| 快速恢复 | 收到唤醒请求后恢复门铃业务和多媒体服务 |
| 保活 CLI | 支持配置空闲检测间隔，便于调试和演示 |

### 2.1 与 doorbell 的差异

| 对比项 | doorbell | doorbell_lp |
| --- | --- | --- |
| 低功耗保活 | 无 | 多媒体空闲后进入保活状态 |
| 多媒体能力 | 摄像头、LCD、音频、图传、BLE 配网 | 与 doorbell 一致 |
| ASR 自动启动 | 是 | 否（defconfig 仍启用，需自行调用） |
| 配置差异 | 标准 doorbell | 额外启用低功耗保活相关配置 |

## 3 快速开始

### 3.1 硬件准备

与 doorbell 相同：BK7259 开发板、MIPI LCD、MIPI 摄像头、UVC 摄像头、Speaker/Mic。doorbell_lp 工程不支持 DVP 摄像头。

### 3.2 编译

```bash
cd projects/doorbell_lp_QN128B832
SDK_DIR=/abs/path/to/avdk_sdk ./dbuild.sh make bk7259 PROJECT=doorbell_lp_QN128B832
```

产物：`projects/doorbell_lp_QN128B832/build/bk7259/doorbell_lp_QN128B832/package/all-app.bin`

### 3.3 演示流程

1. 正常使用 BekenIot APK 配网并开启图传（同 doorbell）
2. 在 APK 上关闭摄像头、音频、LCD 等所有多媒体功能
3. 等待约 10 秒（默认空闲检测间隔），系统自动进入保活状态
4. 服务器下发唤醒命令后，设备恢复图传/音频

### 3.4 保活 CLI

```text
ka                          # 显示帮助
ka interval <ms>            # 设置空闲检测间隔（最小 3000ms，最大 300000ms，持久化到 Flash）
```

## 4 工程目录

```text
projects/doorbell_lp_QN128B832
├── ap/
│   ├── ap_main.c                 # doorbell 配置 + 保活初始化
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

## 5 常见问题

**Q: 进入保活后如何确认功耗变化？**

A: 使用功耗仪对比关闭多媒体服务前后的电流。

**Q: 保活状态下 BLE 还能配网吗？**

A: 不建议在保活状态下配网。需要配网时先唤醒设备，再按 doorbell 的配网流程操作。

**Q: 空闲检测间隔有什么作用？**

A: 空闲检测间隔用于判断多媒体服务关闭后多久进入保活状态，可通过 CLI 调整。
