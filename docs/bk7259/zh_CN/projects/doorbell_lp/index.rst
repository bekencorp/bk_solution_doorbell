Doorbell_lp 项目（BK7259 SMP 低功耗保活方案）
=================================================


:link_to_translation:`en:[English]`

1 项目概述
--------------


本项目是 doorbell 解决方案中的 :strong:`低功耗保活方案`，在 ``doorbell`` 基础上增加了 AP 掉电低功耗保活功能。

:strong:`核心思路`：当设备无活跃多媒体服务（摄像头/音频/LCD 均关闭）时，AP 完全掉电，仅 CP 维持 TCP 长连接并定期发送心跳；收到服务器唤醒命令后，CP 快速上电 AP 并恢复 doorbell 业务。

多媒体能力（摄像头、LCD、音频、图传、BLE 配网等）与 ``doorbell`` 一致，详见 `doorbell 工程文档 <../doorbell/index.html>`_。

CPU 分工（相对 doorbell 的差异见 2.2）：

- :strong:`CP` （M52）：``bk_init()`` 后 ``db_ipc_msg_init()`` 初始化 IPC，``pl_wakeup_host()`` 上电 AP；保活模式下运行 keepalive / db_pack / powerctrl（不再调用 ``bk_start_ap_system()``）。
- :strong:`AP` （M55）：正常运行时与 doorbell 相同；保活时被 CP 掉电，唤醒后重新启动业务。

2 功能特性
--------------


2.1 低功耗保活
,,,,,,,,,,,,,,,,,,


+----------------+-------------------------------------------------------+
| 特性           | 说明                                                  |
+================+=======================================================+
| AP 掉电保活    | 保活模式下 AP 完全掉电，仅 CP 运行                    |
+----------------+-------------------------------------------------------+
| 低电压深度睡眠 | CP 设置 Wi-Fi DTIM=10                                 |
+----------------+-------------------------------------------------------+
| RTC 定时唤醒   | 默认 30 秒间隔发送心跳                                |
+----------------+-------------------------------------------------------+
| TCP 长连接     | CP 侧维持 TCP 连接，db_pack 协议打包                  |
+----------------+-------------------------------------------------------+
| 快速唤醒       | 收到``DBCMD_WAKE_UP_REQUEST`` 后 CP 上电 AP           |
+----------------+-------------------------------------------------------+
| CIF 过滤器     | 允许保活服务器数据包在 AP 掉电时唤醒系统              |
+----------------+-------------------------------------------------------+
| CP 端口绑定    | socket 绑定 0x1000–0x1010，确保对端回包送达 CP 协议栈 |
+----------------+-------------------------------------------------------+
| 断连重连       | 连续 3 次心跳无响应判定链路断开，触发 AP 唤醒         |
+----------------+-------------------------------------------------------+


2.2 与 doorbell 的差异
,,,,,,,,,,,,,,,,,,,,,,,,,,


+--------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------+
| 对比项             | doorbell                       | doorbell_lp                                                                                                          |
+====================+================================+======================================================================================================================+
| 低功耗保活         | 无                             | AP 掉电 + CP TCP 心跳                                                                                                |
+--------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------+
| ap_main 额外初始化 | —                              | ``doorbell_ipc_wakeup_env_init()``、``doorbell_keepalive_handle_wakeup_reason()``、``doorbell_keepalive_cli_init()`` |
+--------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------+
| ASR 自动启动       | 是                             | 否（defconfig 仍启用，需自行调用）                                                                                   |
+--------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------+
| CP 启动 AP 方式    | ``bk_start_ap_system()``       | ``pl_wakeup_host(POWERUP_POWER_WAKEUP_FLAG)``                                                                        |
+--------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------+
| CP 侧模块          | Wi-Fi/BLE Controller + 启动 AP | keepalive / db_ipc_msg / db_pack / powerctrl + AP 上下电                                                             |
+--------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------+
| defconfig          | 标准 doorbell                  | 额外 ``CONFIG_NTWK_CLIENT_SERVICE_ENABLE``；CP 侧启用 ``CONFIG_PM_AP_POWERDOWN_WHEN_LV`` 等低功耗选项                |
+--------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------+


3 快速开始
--------------


3.1 硬件准备
,,,,,,,,,,,,,,,,


与 doorbell 相同：BK7259 开发板、MIPI LCD、MIPI/UVC 摄像头、Speaker/Mic。

3.2 编译
,,,,,,,,,,,,


.. code-block:: bash

   cd projects/doorbell_lp
   SDK_DIR=/abs/path/to/avdk_sdk ./dbuild.sh make bk7259 PROJECT=doorbell_lp



产物：``projects/doorbell_lp/build/bk7259/doorbell_lp/package/all-app.bin``

3.3 演示流程
,,,,,,,,,,,,,,,,


1. 正常使用 BekenIot APK 配网并开启图传（同 doorbell）
2. 在 APK 上关闭摄像头、音频、LCD 等所有多媒体功能
3. 等待约 10 秒（默认空闲检测间隔），系统自动进入保活模式
4. 此时 AP 掉电，CP 维持 TCP 心跳，功耗显著降低
5. 服务器下发唤醒命令后，设备恢复图传/音频

3.4 保活 CLI
,,,,,,,,,,,,,,,,


.. code-block:: text

   ka                          # 显示帮助
   ka interval <ms>            # 设置空闲检测间隔（最小 3000ms，最大 300000ms，持久化到 Flash）



4 低功耗保活实现机制
------------------------


4.1 整体架构
,,,,,,,,,,,,,,,,


.. code-block:: 

   ┌─────────────────────────────────────────────────────────────┐
   │  AP（M55）— 正常运行时                                       │
   │  doorbell_core / 多媒体 / BLE / 图传                        │
   │  doorbell_keepalive.c：监控 MM 状态，触发保活               │
   └──────────────────────┬──────────────────────────────────────┘
                          │ IPC (doorbell_ipc_msg)
   ┌──────────────────────▼──────────────────────────────────────┐
   │  CP（M52）— 保活模式                                      │
   │  db_keepalive.c：关闭 AP → TCP 连接 → RTC 心跳 → RX 唤醒    │
   │  db_pack.c：数据打包（魔数/CRC/序列号）                      │
   │  powerctrl.c：AP 上下电控制                                  │
   └─────────────────────────────────────────────────────────────┘



4.2 保活启动流程
,,,,,,,,,,,,,,,,,,,,


.. code-block:: 

   AP 定时检查 doorbell_mm_service_get_status()（默认 10 秒）
       ↓
   mm_status == 0（无 camera / audio / lcd 活跃）
       ↓
   doorbell_keepalive_stop_service_if_running() 停止 TCP/UDP 图传
       ↓
   从 Flash 读取服务器 IP/端口（doorbell_get_ntwk_service_info_from_flash）
       ↓
   doorbell_ipc_start_keepalive(ip, port) → CP
       ↓
   CP db_keepalive_cp_init() + db_keepalive_cp_start()
       ↓
   TX 线程：pl_power_down_host() 关闭 AP
       ↓
   配置 CIF 过滤器 + 绑定 CP 端口 + TCP connect（最多重试 5 次）
       ↓
   创建 RX 线程 + 进入低电压睡眠（PM_MODE_LOW_VOLTAGE）
       ↓
   发送首个心跳（DBCMD_KEEP_ALIVE_REQUEST），进入 RTC 30 秒循环



:strong:`注意`：若当前为 CS2 P2P 模式，需先切换到 TCP/UDP，保活仅支持 TCP。

4.3 唤醒流程
,,,,,,,,,,,,,,,,


.. code-block:: 

   服务器 → DBCMD_WAKE_UP_REQUEST
       ↓
   CP RX 线程 db_pack_unpack() 解包
       ↓
   pl_wakeup_host(POWERUP_MULTIMEDIA_WAKEUP_HOST_FLAG)
       ↓
   AP 重新上电启动
       ↓
   doorbell_keepalive_handle_wakeup_reason()
       ├── 禁用 BLE
       ├── doorbell_ipc_stop_keepalive() 停止 CP 保活
       └── 从 Flash 恢复 TCP/UDP 服务
       ↓
   发送待处理的唤醒应答命令
       ↓
   恢复正常 doorbell 业务



4.4 唤醒原因标志
,,,,,,,,,,,,,,,,,,,,


定义见 ``doorbell_keepalive.h``：

+-----------------------------------------+----+------------------------------+
| 标志                                    | 值 | 含义                         |
+=========================================+====+==============================+
| ``POWERUP_POWER_WAKEUP_FLAG``           | 1  | 正常上电                     |
+-----------------------------------------+----+------------------------------+
| ``POWERUP_MULTIMEDIA_WAKEUP_HOST_FLAG`` | 2  | 多媒体唤醒请求（服务器下发） |
+-----------------------------------------+----+------------------------------+
| ``POWERUP_KEEPALIVE_DISCONNECTION``     | 3  | 保活断连唤醒                 |
+-----------------------------------------+----+------------------------------+
| ``POWERUP_KEEPALIVE_FAIL_WAKEUP_FLAG``  | 4  | 保活失败唤醒                 |
+-----------------------------------------+----+------------------------------+


4.5 AP 侧实现细节
,,,,,,,,,,,,,,,,,,,,,


:strong:`多媒体状态位` （``doorbell_mm_service_get_status()``）：

- ``MM_STATUS_CAMERA_MASK``：摄像头/H.264 图传
- ``MM_STATUS_AUDIO_MASK``：双向音频
- ``MM_STATUS_LCD_MASK``：LCD 显示

三者均为 0 时视为空闲，触发保活倒计时。

:strong:`保活触发逻辑` （``doorbell_keepalive.c``）：

1. 定时器到期 → 检查 mm_status
2. 若空闲 → 尝试停止当前网络服务
3. 服务停止成功后（或无需停止）→ 发送 IPC 保活启动命令
4. 若服务仍在运行（如有活跃会话）→ 跳过，等待下次检查

4.6 CP 侧实现细节
,,,,,,,,,,,,,,,,,,,,,


:strong:`TX 线程主流程` （``db_keepalive_tx_handler``）：

1. ``pl_power_down_host()`` — AP 完全掉电
2. ``cif_filter_add_customer_filter(server, port)`` — CIF 过滤器
3. ``bind()`` 到 CP 端口范围（0x1000–0x1010）
4. TCP ``connect()`` 到服务器（超时 3 秒，最多重试 5 次，间隔 1 秒）
5. 创建 RX 线程
6. ``db_keepalive_start_lv_sleep()`` — 低电压睡眠
7. 循环：设置 RTC 30 秒 → 等待信号量 → 发送心跳 → 重复

:strong:`RX 线程` （``db_keepalive_rx_handler``）：

- 阻塞接收服务器数据 → ``db_pack_unpack()`` 解包
- ``DBCMD_KEEP_ALIVE_RESPONSE``：心跳成功
- ``DBCMD_WAKE_UP_REQUEST``：调用 ``pl_wakeup_host()`` 唤醒 AP

:strong:`数据打包协议` （``cp/db_pack/``）：

- 魔数验证、序列号递增、CRC 校验
- 支持分包/合包

:strong:`CP 端口绑定机制`：

LwIP 默认临时端口范围 0xc000–0xffff，不在 CP 本地端口范围（0x1000–0x1010）内。
CP 接收通路按目的端口分流：落在 CP 范围内的报文送 CP 协议栈，否则送 AP。
保活 socket 显式 bind 到 CP 范围，确保对端回包由 CP 正确处理。

4.7 配置参数
,,,,,,,,,,,,,,,,


+----------------+---------------+-----------------------------------------+-----------------------+
| 参数           | 默认值        | 宏定义                                  | 说明                  |
+================+===============+=========================================+=======================+
| 心跳间隔       | 30 秒         | ``DB_KEEPALIVE_DEFAULT_INTERVAL_MS``    | CP 侧 RTC 唤醒周期    |
+----------------+---------------+-----------------------------------------+-----------------------+
| Socket 超时    | 3 秒          | ``DB_KEEPALIVE_SOCKET_TIMEOUT_MS``      | TCP connect/recv 超时 |
+----------------+---------------+-----------------------------------------+-----------------------+
| 连接最大重试   | 5 次          | ``DB_KEEPALIVE_MAX_RETRY_CNT``          | TCP 建连失败重试      |
+----------------+---------------+-----------------------------------------+-----------------------+
| 无响应上限     | 3 次          | ``DB_KEEPALIVE_MAX_NO_RESP_CNT``        | 连续无心跳响应则断连  |
+----------------+---------------+-----------------------------------------+-----------------------+
| 消息缓冲区     | 1460 字节     | ``DB_KEEPALIVE_MSG_BUFFER_SIZE``        | 单包最大长度          |
+----------------+---------------+-----------------------------------------+-----------------------+
| RTC 最小间隔   | 500 ms        | ``DB_KEEPALIVE_RTC_TIMER_THRESHOLD_MS`` |                       |
+----------------+---------------+-----------------------------------------+-----------------------+
| 空闲检测间隔   | 10 秒         | ``MM_STATUS_CHECK_INTERVAL_MS``         | AP 侧，CLI 可改       |
+----------------+---------------+-----------------------------------------+-----------------------+
| 空闲检测最小值 | 3 秒          | ``MM_STATUS_CHECK_MIN_INTERVAL_MS``     | CLI 下限              |
+----------------+---------------+-----------------------------------------+-----------------------+
| CP 绑定端口    | 0x1000–0x1010 | ``DB_KEEPALIVE_CP_BIND_PORT_START/END`` | 17 个端口             |
+----------------+---------------+-----------------------------------------+-----------------------+


5 API 参考
--------------


5.1 AP 侧（``components/smart_lock/``）
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,


:strong:`保活管理` （``doorbell_keepalive.h``）：

.. code-block:: c

   void doorbell_keepalive_handle_wakeup_reason(void);
   void doorbell_keepalive_on_service_start_success(void);
   void doorbell_keepalive_on_keepalive_disconnection(void);
   bk_err_t doorbell_keepalive_start_mm_status_check(void);
   bk_err_t doorbell_keepalive_stop_mm_status_check(void);
   int doorbell_keepalive_cli_init(void);



:strong:`IPC 通信` （``doorbell_ipc_msg.h``）：

.. code-block:: c

   bk_err_t doorbell_ipc_start_keepalive(const char *ip_addr, const char *cmd_port);
   bk_err_t doorbell_ipc_stop_keepalive(void);
   int doorbell_ipc_wakeup_env_init(void);



IPC 命令枚举：

+------------------------------------------------+--------+---------+
| 命令                                           | 值     | 方向    |
+================================================+========+=========+
| ``DOORBELL_IPC_CMD_KEEPALIVESTART``            | 0x0002 | AP → CP |
+------------------------------------------------+--------+---------+
| ``DOORBELL_IPC_CMD_KEEPALIVESTOP``             | 0x0003 | AP → CP |
+------------------------------------------------+--------+---------+
| ``DOORBELL_IPC_EVENT_KEEPALIVE_DISCONNECTION`` | 0x1002 | CP → AP |
+------------------------------------------------+--------+---------+


5.2 CP 侧（``cp/keepalive/db_keepalive.h``）
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,


.. code-block:: c

   bk_err_t db_keepalive_cp_init(db_ipc_keepalive_cfg_t *cfg);
   bk_err_t db_keepalive_cp_deinit(void);
   bk_err_t db_keepalive_cp_start(void);
   bk_err_t db_keepalive_cp_stop(void);



5.3 电源管理（``cp/powerctrl/powerctrl.h``）
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,


.. code-block:: c

   void pl_wakeup_host(uint32_t flag);
   void pl_power_down_host(void);



6 工程目录
--------------


.. code-block:: 

   projects/doorbell_lp
   ├── ap/
   │   ├── ap_main.c                 # doorbell 配置 + 保活初始化
   │   ├── audio_param/
   │   └── config/bk7259_ap/defconfig
   ├── cp/
   │   ├── cp_main.c
   │   ├── keepalive/                # CP 保活服务（TX/RX 线程、RTC、心跳）
   │   │   ├── db_keepalive.c
   │   │   └── db_keepalive.h
   │   ├── db_ipc_msg/               # AP↔CP IPC 消息路由
   │   ├── db_pack/                  # 数据打包协议
   │   └── powerctrl/                # AP 上下电（pl_wakeup_host / pl_power_down_host）
   ├── partitions/bk7259/
   ├── CMakeLists.txt
   ├── Makefile
   └── dbuild.sh



7 常见问题
--------------


:strong:`Q: 进入保活后如何确认 AP 已掉电？`

A: 串口不再有 AP 侧 log 输出（仅 CP log）。可用功耗仪对比保活前后电流。

:strong:`Q: 保活模式下 BLE 还能配网吗？`

A: 不能。BLE 运行在 AP 侧，AP 掉电后 BLE 不可用。唤醒后 AP 重启，需重新配网或从 Flash 恢复 Wi-Fi 配置。

:strong:`Q: 空闲检测间隔和心跳间隔有什么区别？`

A: 空闲检测（默认 10 秒）是 AP 侧判断"是否可以进入保活"的周期；心跳间隔（30 秒）是 CP 侧维持 TCP 连接的周期。两者独立，均可通过代码/CLI 调整。

:strong:`Q: CS2 P2P 模式下能否保活？`

A: 不能。保活前会自动尝试切换到 TCP/UDP 模式。建议产品部署时使用 TCP 服务模式以支持低功耗保活。