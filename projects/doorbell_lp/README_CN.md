# Doorbell_lp项目开发指南

* [English](./README.md)

## 1 项目概述

本项目是基于BK7258芯片的智能门铃门锁 **低功耗保活解决方案**，在doorbell工程基础上增加了 **AP掉电低功耗保活功能**。

## 2 功能特性

### 2.1 低功耗保活功能

* **AP掉电低功耗保活**：保活模式下，AP（应用核）完全掉电关闭，仅CP（协处理核）运行，实现极低功耗
* **低电压深度睡眠**：AP掉电后，系统进入低电压睡眠模式(dtim=10)，功耗降至最低
* **RTC定时器唤醒**：使用RTC定时器实现低功耗唤醒，默认间隔30秒
* **TCP长连接保活**：维持与服务器的TCP连接，定期发送心跳包
* **快速唤醒恢复**：收到唤醒命令时，快速唤醒AP（上电启动）并启动多媒体服务
* **CIF过滤器**：配置CIF过滤器，允许保活服务器数据包唤醒系统（即使AP处于掉电状态）

### 2.2 多媒体业务功能

doorbell_lp工程的多媒体业务功能（摄像头、LCD显示、音频等）与doorbell工程完全一致，详细说明请参考 `Doorbell项目开发指南 <../doorbell/index.html>`_。

## 3 快速开始

### 3.1 硬件准备

* BK7258开发板
* LCD屏幕
* UVC/DVP摄像头模块
* 板载speaker/mic，或UAC
* 电源和连接线

### 3.2 编译和烧录

编译流程参考 `Doorbell 解决方案 <../../index.html>`_

烧录流程参考 `烧录代码 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.0.1/get-started/index.html#id7>`_

编译生成的烧录bin文件路径：``projects/doorbell_lp/build/bk7258/doorbell_lp/package/all-app.bin``

### 3.3 基本操作流程

doorbell_lp工程的基本操作流程与doorbell工程一致，详细说明请参考 `Doorbell项目开发指南 <../doorbell/index.html>`_ 的"3.3 基本操作流程"章节。

**注意**：设备正常运行多媒体服务后，当无活跃多媒体服务时，系统会自动进入低功耗保活模式（AP掉电），详细机制请参考本文档"4 低功耗保活实现机制"章节。

## 4 低功耗保活实现机制

### 4.1 整体架构

doorbell_lp工程采用双核架构实现低功耗保活功能：

![Doorbell LP 架构图](../../docs/bk7258/_static/doorbell_lp.png)


### 4.2 工作流程

#### 4.2.1 保活启动流程

```
AP侧检测无活跃服务（30秒定时检查）
    ↓
停止当前网络传输服务（TCP/UDP）
    ↓
从Flash读取服务器信息
    ↓
通过IPC发送保活启动命令到CP
    ↓
CP侧初始化保活模块
    ↓
CP侧关闭AP（掉电）← 【核心：AP完全掉电关闭】
    ↓
CP侧配置CIF过滤器
    ↓
CP侧建立TCP连接（最多重试5次）
    ↓
CP侧创建RX线程接收服务器数据
    ↓
CP侧启动低电压睡眠
    ↓
发送第一个心跳包
    ↓
进入保活循环（RTC定时器30秒唤醒 → 发送心跳包）
    ↓
【AP处于掉电状态，仅CP运行，功耗极低】
```

#### 4.2.2 唤醒流程

```
服务器发送唤醒命令
    ↓
CP侧RX线程接收并解析（AP处于掉电状态）
    ↓
CP侧唤醒AP（上电）
    ↓
AP侧重新初始化并启动
    ↓
AP侧读取唤醒原因
    ↓
AP侧停止CP保活，启动多媒体服务
    ↓
系统恢复正常工作模式（AP运行，CP协同）
```

### 4.3 AP侧实现

#### 4.3.1 多媒体服务状态监控

AP侧通过定时器（30秒间隔）持续监控多媒体服务状态：

- 检查多媒体服务状态（`doorbell_mm_service_get_status()`）
- 如果无活跃多媒体服务且距离上次更新超过30秒，触发保活启动
- 如果多媒体服务正在运行，先停止网络传输服务（TCP/UDP）

#### 4.3.2 保活启动触发

当检测到无活跃多媒体服务时，触发保活启动流程：

1. 停止当前网络传输服务
   - 调用 `db_keepalive_stop_service_if_running()`
   - 检查Flash中的服务类型（TCP/UDP）
   - 注意：如果当前服务是CS2模式，需要先切换到TCP/UDP模式，因为低功耗保活模式不支持CS2

2. 发送保活命令
   - 从Flash读取服务器IP和端口
   - 通过 `db_ipc_start_keepalive()` 发送IPC命令到CP侧
   - 命令类型：`DB_IPC_CMD_KEEPALIVESTART`

#### 4.3.3 唤醒处理

当系统从低功耗模式唤醒时，AP侧处理唤醒原因：

- 读取唤醒原因（`pl_wakeup_env->wakeup_reason`）
- 如果是多媒体唤醒请求（`POWERUP_MULTIMEDIA_WAKEUP_HOST_FLAG`）：
  - 禁用蓝牙
  - 停止CP侧保活
  - 从Flash启动服务（根据Flash中保存的服务类型启动TCP或UDP服务）
  - 保存待处理的唤醒命令，等待服务启动成功后发送

### 4.4 CP侧实现

#### 4.4.1 保活服务初始化

CP侧收到保活启动命令后的初始化流程：

1. 初始化保活模块
   - 分配接收缓冲区
   - 初始化数据打包协议（db_pack）
   - 初始化信号量（用于RTC唤醒）

2. 创建TX线程
   - 启动 `db_keepalive_tx_handler()` 线程
   - 负责建立连接、发送心跳包和管理低功耗状态

#### 4.4.2 TX线程主流程

TX线程的主要工作流程：

1. 关闭AP（掉电）
   - 调用 `pl_power_down_host()` 完全关闭AP电源
   - AP进入掉电状态，所有AP侧功能停止运行

2. 配置CIF过滤器
   - 调用 `cif_filter_add_customer_filter(server, port)`
   - 允许保活服务器数据包唤醒系统

3. 建立TCP连接
   - 创建TCP socket
   - 设置超时时间（3秒）
   - 连接服务器（最多重试5次）

4. 创建RX线程
   - 启动 `db_keepalive_rx_handler()` 线程
   - 持续接收服务器数据，使用 `db_pack_unpack()` 解包

5. 启动低电压睡眠
   - 调用 `db_keepalive_start_lv_sleep()`
   - 设置低功耗状态为 `PM_MODE_LOW_VOLTAGE`

6. 进入保活循环
   - 设置RTC定时器（30秒）
   - 等待信号量（RTC唤醒）
   - 发送心跳包
   - 循环执行

#### 4.4.3 RTC定时器唤醒机制

CP侧使用RTC（Real-Time Clock）定时器实现低功耗唤醒：

**说明：** 在AP完全掉电关闭的状态下，系统通过RTC定时器实现低功耗唤醒。

- 设置RTC定时器：间隔30秒，唤醒源为RTC中断，睡眠模式为低电压睡眠
- 系统进入低电压睡眠：AP已完全掉电关闭，仅保留必要唤醒电路，功耗降至最低
- RTC定时器触发：30秒后触发 `db_keepalive_rtc_timer_handler()`，触发信号量唤醒TX线程
- TX线程被唤醒：发送心跳包，重新设置RTC定时器，再次进入低电压睡眠

#### 4.4.4 心跳包发送机制

心跳包发送流程：

1. 构造keepalive请求
   - 使用 `db_evt_head_t` 结构体
   - Opcode: `DBCMD_KEEP_ALIVE_REQUEST`

2. 数据打包
   - 使用 `db_pack_pack()` 添加协议头（魔数、序列号、长度、CRC）

3. 发送到服务器
   - 调用 `send()` 发送数据
   - 检查发送结果，记录日志

4. 服务器响应
   - RX线程接收原始数据
   - 使用 `db_pack_unpack()` 解包
   - 如果收到 `DBCMD_KEEP_ALIVE_RESPONSE`，记录日志表示保活成功

#### 4.4.5 唤醒命令处理

当服务器发送唤醒命令时，CP侧的处理流程：

1. RX线程接收数据
   - 使用 `db_keepalive_recv_raw_data()` 接收原始数据
   - 使用 `db_pack_unpack()` 解包
   - 通过 `db_keepalive_unpack_callback()` 回调解析命令头

2. 检测到唤醒命令
   - Opcode: `DBCMD_WAKE_UP_REQUEST`
   - 通过 `db_tx_cmd_recive_callback()` 处理命令

3. 唤醒AP（上电）
   - 调用 `pl_wakeup_host(POWERUP_MULTIMEDIA_WAKEUP_HOST_FLAG)`
   - 设置唤醒原因为 `POWERUP_MULTIMEDIA_WAKEUP_HOST_FLAG`
   - 启动AP电源（AP从掉电状态恢复上电）
   - 通过CIF唤醒AP

4. AP侧处理唤醒
   - AP重新初始化并启动
   - 读取唤醒原因
   - 停止CP侧保活
   - 启动多媒体服务

### 4.5 关键技术点

#### 4.5.1 低电压睡眠模式（AP掉电）

**说明：** 在低功耗保活模式下，AP完全掉电关闭，系统进入低电压睡眠模式，实现极低功耗。

- 进入条件：无多媒体服务运行，保活连接已建立，AP已完全掉电关闭
- AP掉电：通过 `pl_power_down_host()` 完全关闭AP电源
- 唤醒源：RTC定时器中断或CIF数据包（即使AP处于掉电状态也能唤醒）
- 功耗：系统进入深度睡眠，设置 dtim=10，AP掉电后功耗极低，仅保留必要的唤醒电路

#### 4.5.2 CIF过滤器

为了在低电压睡眠期间仍能接收保活服务器的数据包，需要配置CIF（Communication Interface）过滤器：

- 允许来自保活服务器的数据包唤醒系统
- 即使系统处于低电压睡眠，也能接收唤醒命令
- 通过 `cif_filter_add_customer_filter()` 配置

#### 4.5.3 数据打包协议

使用 `db_pack` 协议确保数据传输的可靠性：

- 魔数验证：确保数据包有效性
- 序列号管理：支持数据包顺序检测
- CRC校验：确保数据完整性
- 支持分包/合包：处理大数据传输

#### 4.5.4 连接重试机制

TCP连接建立失败时自动重试：

- 最多重试5次
- 每次重试间隔1秒
- 确保在网络不稳定时仍能建立连接

### 4.6 配置参数

- **保活间隔**：30秒（`DB_KEEPALIVE_DEFAULT_INTERVAL_MS = 30 * 1000`）
- **Socket超时**：3秒（`DB_KEEPALIVE_SOCKET_TIMEOUT_MS = 3000`）
- **最大重试次数**：5次（`DB_KEEPALIVE_MAX_RETRY_CNT = 5`）
- **消息缓冲区大小**：1460字节（`DB_KEEPALIVE_MSG_BUFFER_SIZE = 1460`）
- **RTC定时器最小间隔**：500毫秒（`DB_KEEPALIVE_RTC_TIMER_THRESHOLD_MS = 500`）
- **多媒体状态检查间隔**：30秒（`MM_STATUS_CHECK_INTERVAL_MS = 30 * 1000`）

## 5 API参考

本章节提供doorbell_lp工程**低功耗保活功能**的API接口说明。

**说明：** 多媒体业务API（摄像头管理、H.264编码、图传、显示、音频等）与doorbell工程完全一致，详细说明请参考 `Doorbell项目开发指南 <../doorbell/index.html>`_ 的 "5 API参考" 章节。

### 5.1 AP侧保活API

#### 5.1.1 db_keepalive_handle_wakeup_reason

```c
/**
 * @brief 处理系统唤醒原因
 *
 * @note 此函数在系统唤醒时调用，处理不同的唤醒原因：
 *       1. 读取唤醒原因（pl_get_wakeup_reason()）
 *       2. 如果是多媒体唤醒请求（POWERUP_MULTIMEDIA_WAKEUP_HOST_FLAG）：
 *          - 禁用蓝牙
 *          - 停止CP侧保活
 *          - 从Flash启动服务
 *          - 发送待处理的唤醒命令
 *
 * @see pl_get_wakeup_reason()
 */
void db_keepalive_handle_wakeup_reason(void);
```

### 5.2 CP侧保活API

#### 5.2.1 db_keepalive_cp_init

```c
/**
 * @brief 初始化CP侧保活模块
 *
 * @param cfg 保活配置结构体指针
 *        - server: 服务器IP地址
 *        - port: 服务器端口
 *
 * @return bk_err_t 操作结果
 *         - BK_OK: 成功
 *         - BK_FAIL: 失败
 * 
 * @note 此函数初始化保活模块：
 *       1. 分配接收缓冲区
 *       2. 初始化数据打包协议（db_pack_init）
 *       3. 初始化信号量（用于RTC唤醒）
 *       4. 设置保活间隔（默认30秒）
 *
 * @warning 调用此函数前应确保配置参数有效
 * @see db_keepalive_cp_start()
 */
bk_err_t db_keepalive_cp_init(db_ipc_keepalive_cfg_t *cfg);
```

#### 5.2.2 db_keepalive_cp_start

```c
/**
 * @brief 启动CP侧保活服务
 *
 * @return bk_err_t 操作结果
 *         - BK_OK: 成功
 *         - BK_FAIL: 失败
 * 
 * @note 此函数启动保活服务：
 *       1. 创建TX线程（db_keepalive_tx_handler）
 *       2. TX线程负责：
 *          - 关闭AP（掉电）
 *          - 建立TCP连接
 *          - 创建RX线程
 *          - 启动低电压睡眠
 *          - 发送心跳包
 *
 * @warning 调用此函数前应先调用db_keepalive_cp_init()
 * @see db_keepalive_cp_init()
 * @see db_keepalive_cp_stop()
 */
bk_err_t db_keepalive_cp_start(void);
```

#### 5.2.3 db_keepalive_cp_stop

```c
/**
 * @brief 停止CP侧保活服务
 * 
 * @return bk_err_t 操作结果
 *         - BK_OK: 成功
 *         - BK_FAIL: 失败
 * 
 * @note 此函数停止保活服务并清理资源：
 *       1. 停止RTC定时器
 *       2. 退出低电压睡眠
 *       3. 关闭TCP连接
 *       4. 删除TX和RX线程
 *       5. 释放所有资源
 *
 * @see db_keepalive_cp_start()
 */
bk_err_t db_keepalive_cp_stop(void);
```

### 5.3 电源管理API

#### 5.3.1 pl_wakeup_host

```c
/**
 * @brief 唤醒AP（应用核）
 *
 * @param flag 唤醒原因标志
 *        - POWERUP_MULTIMEDIA_WAKEUP_HOST_FLAG: 多媒体唤醒请求
 *        - POWERUP_KEEPALIVE_WAKEUP_FLAG: 保活唤醒
 *
 * @note 此函数唤醒AP并设置唤醒原因：
 *       1. 设置唤醒原因（pl_set_wakeup_reason）
 *       2. 启动AP电源（bk_pm_module_vote_boot_cp1_ctrl）
 *       3. 通过CIF唤醒AP（cif_power_up_host）
 */
void pl_wakeup_host(uint32_t flag);
```

#### 5.3.2 pl_power_down_host

```c
/**
 * @brief 关闭AP（应用核）
 *
 * @note 此函数关闭AP电源：
 *       1. 重置唤醒原因（pl_reset_wakeup_reason）
 *       2. 关闭AP电源（bk_pm_module_vote_boot_cp1_ctrl）
 *       3. 通过CIF关闭AP（cif_power_down_host）
 */
void pl_power_down_host(void);
```
