Doorbell_lp Project (BK7259 SMP Low-Power Keepalive)
========================================================


:link_to_translation:`zh_CN:[中文]`

1 Overview
--------------


Low-power keepalive firmware in the doorbell solution, extending ``doorbell`` with AP power-down keepalive.

:strong:`Core idea`: when no multimedia services are active (camera/audio/LCD all off), the AP is fully powered down and only the CP maintains a TCP long connection with periodic heartbeats. On server wake command, CP quickly powers up AP and restores doorbell business.

Multimedia capabilities match ``doorbell`` — see `doorbell project documentation <../doorbell/index.html>`_.

CPU split (see section 2.2 for differences vs doorbell):

- :strong:`CP` (M52): after ``bk_init()``, runs ``db_ipc_msg_init()`` and ``pl_wakeup_host()`` to power up AP; in keepalive mode runs keepalive / db_pack / powerctrl (does :strong:`not` call ``bk_start_ap_system()``).
- :strong:`AP` (M55): same as doorbell during normal operation; powered down by CP in keepalive mode, reboots on wake.

2 Features
--------------


2.1 Low-power keepalive
,,,,,,,,,,,,,,,,,,,,,,,,,,,


+-------------------------+--------------------------------------------------------------+
| Feature                 | Description                                                  |
+=========================+==============================================================+
| AP power-down keepalive | AP fully off in keepalive mode; only CP runs                 |
+-------------------------+--------------------------------------------------------------+
| Low-voltage deep sleep  | CP sets Wi-Fi DTIM=10                                        |
+-------------------------+--------------------------------------------------------------+
| RTC periodic wake       | default 30 s heartbeat interval                              |
+-------------------------+--------------------------------------------------------------+
| TCP long connection     | CP maintains TCP with db_pack framing                        |
+-------------------------+--------------------------------------------------------------+
| Fast wake               | ``DBCMD_WAKE_UP_REQUEST`` → CP powers up AP                  |
+-------------------------+--------------------------------------------------------------+
| CIF filter              | server packets can wake system while AP is off               |
+-------------------------+--------------------------------------------------------------+
| CP port binding         | socket bound to 0x1000–0x1010 so peer replies reach CP stack |
+-------------------------+--------------------------------------------------------------+
| Disconnect recovery     | 3 missed heartbeats → link dead → wake AP                    |
+-------------------------+--------------------------------------------------------------+


2.2 vs doorbell
,,,,,,,,,,,,,,,,,,,


+---------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------+
| Item                | doorbell                       | doorbell_lp                                                                                                          |
+=====================+================================+======================================================================================================================+
| Low-power keepalive | none                           | AP off + CP TCP heartbeat                                                                                            |
+---------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------+
| ap_main extras      | —                              | ``doorbell_ipc_wakeup_env_init()``, ``doorbell_keepalive_handle_wakeup_reason()``, ``doorbell_keepalive_cli_init()`` |
+---------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------+
| ASR auto-start      | yes                            | no (defconfig enabled, call manually)                                                                                |
+---------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------+
| CP boots AP via     | ``bk_start_ap_system()``       | ``pl_wakeup_host(POWERUP_POWER_WAKEUP_FLAG)``                                                                        |
+---------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------+
| CP modules          | Wi-Fi/BLE Controller + boot AP | keepalive / db_ipc_msg / db_pack / powerctrl + AP power control                                                      |
+---------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------+
| defconfig           | standard doorbell              | + ``CONFIG_NTWK_CLIENT_SERVICE_ENABLE``; CP enables ``CONFIG_PM_AP_POWERDOWN_WHEN_LV`` etc.                          |
+---------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------+


3 Quick Start
-----------------


3.1 Hardware
,,,,,,,,,,,,,,,,


Same as doorbell: BK7259 board, MIPI LCD, MIPI/UVC camera, speaker/mic.

3.2 Build
,,,,,,,,,,,,,


.. code-block:: bash

   cd projects/doorbell_lp
   SDK_DIR=/abs/path/to/avdk_sdk ./dbuild.sh make bk7259 PROJECT=doorbell_lp



Output: ``projects/doorbell_lp/build/bk7259/doorbell_lp/package/all-app.bin``

3.3 Demo
,,,,,,,,,,,,


1. Normal BekenIot APK provisioning and streaming (same as doorbell)
2. Turn off all multimedia in APK (camera, audio, LCD)
3. Wait ~10 s (default idle check interval) → system enters keepalive
4. AP powered down, CP sends TCP heartbeats, power drops significantly
5. Server wake command → device resumes streaming/audio

3.4 Keepalive CLI
,,,,,,,,,,,,,,,,,,,,,


.. code-block:: text

   ka                          # show help
   ka interval <ms>            # idle check interval (3000–300000 ms, persisted to Flash)



4 Keepalive Implementation
------------------------------


4.1 Architecture
,,,,,,,,,,,,,,,,,,,,


.. code-block:: 

   ┌─────────────────────────────────────────────────────────────┐
   │  AP (M55) — normal operation                              │
   │  doorbell_core / multimedia / BLE / streaming             │
   │  doorbell_keepalive.c: monitor MM status, trigger keepalive │
   └──────────────────────┬──────────────────────────────────────┘
                          │ IPC (doorbell_ipc_msg)
   ┌──────────────────────▼──────────────────────────────────────┐
   │  CP (M52) — keepalive mode                                 │
   │  db_keepalive.c: power down AP → TCP → RTC heartbeat → RX wake│
   │  db_pack.c: framing (magic/CRC/sequence)                      │
   │  powerctrl.c: AP power on/off                                 │
   └─────────────────────────────────────────────────────────────┘



4.2 Keepalive start flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,


.. code-block:: 

   AP periodically checks doorbell_mm_service_get_status() (default 10 s)
       ↓
   mm_status == 0 (no active camera / audio / lcd)
       ↓
   doorbell_keepalive_stop_service_if_running() stops TCP/UDP streaming
       ↓
   read server IP/port from Flash (doorbell_get_ntwk_service_info_from_flash)
       ↓
   doorbell_ipc_start_keepalive(ip, port) → CP
       ↓
   CP db_keepalive_cp_init() + db_keepalive_cp_start()
       ↓
   TX thread: pl_power_down_host() powers down AP
       ↓
   CIF filter + CP port bind + TCP connect (retry up to 5 times)
       ↓
   create RX thread + enter low-voltage sleep (PM_MODE_LOW_VOLTAGE)
       ↓
   send first heartbeat (DBCMD_KEEP_ALIVE_REQUEST), enter 30 s RTC loop



:strong:`Note`: if CS2 P2P mode is active, switch to TCP/UDP first — keepalive is TCP-only.

4.3 Wake-up flow
,,,,,,,,,,,,,,,,,,,,


.. code-block:: 

   Server → DBCMD_WAKE_UP_REQUEST
       ↓
   CP RX thread db_pack_unpack()
       ↓
   pl_wakeup_host(POWERUP_MULTIMEDIA_WAKEUP_HOST_FLAG)
       ↓
   AP reboots
       ↓
   doorbell_keepalive_handle_wakeup_reason()
       ├── disable BLE
       ├── doorbell_ipc_stop_keepalive() stops CP keepalive
       └── restore TCP/UDP service from Flash
       ↓
   send pending wake response
       ↓
   resume normal doorbell business



4.4 Wake flags
,,,,,,,,,,,,,,,,,,


Defined in ``doorbell_keepalive.h``:

+-----------------------------------------+-------+--------------------------------+
| Flag                                    | Value | Meaning                        |
+=========================================+=======+================================+
| ``POWERUP_POWER_WAKEUP_FLAG``           | 1     | normal power-on                |
+-----------------------------------------+-------+--------------------------------+
| ``POWERUP_MULTIMEDIA_WAKEUP_HOST_FLAG`` | 2     | server multimedia wake request |
+-----------------------------------------+-------+--------------------------------+
| ``POWERUP_KEEPALIVE_DISCONNECTION``     | 3     | keepalive disconnect wake      |
+-----------------------------------------+-------+--------------------------------+
| ``POWERUP_KEEPALIVE_FAIL_WAKEUP_FLAG``  | 4     | keepalive failure wake         |
+-----------------------------------------+-------+--------------------------------+


4.5 AP-side details
,,,,,,,,,,,,,,,,,,,,,,,


:strong:`Multimedia status bits` (``doorbell_mm_service_get_status()``):

- ``MM_STATUS_CAMERA_MASK``: camera / H.264 stream
- ``MM_STATUS_AUDIO_MASK``: full-duplex audio
- ``MM_STATUS_LCD_MASK``: LCD display

All three zero → idle → keepalive countdown starts.

:strong:`Trigger logic` (``doorbell_keepalive.c``):

1. Timer fires → check mm_status
2. If idle → try to stop current network service
3. After service stops (or no stop needed) → send IPC keepalive start
4. If service still running (active session) → skip, wait for next check

4.6 CP-side details
,,,,,,,,,,,,,,,,,,,,,,,


:strong:`TX thread` (``db_keepalive_tx_handler``):

1. ``pl_power_down_host()`` — AP fully off
2. ``cif_filter_add_customer_filter(server, port)`` — CIF filter
3. ``bind()`` to CP port range (0x1000–0x1010)
4. TCP ``connect()`` to server (3 s timeout, up to 5 retries, 1 s apart)
5. create RX thread
6. ``db_keepalive_start_lv_sleep()`` — low-voltage sleep
7. loop: set RTC 30 s → wait semaphore → send heartbeat → repeat

:strong:`RX thread` (``db_keepalive_rx_handler``):

- block receive → ``db_pack_unpack()``
- ``DBCMD_KEEP_ALIVE_RESPONSE``: heartbeat OK
- ``DBCMD_WAKE_UP_REQUEST``: call ``pl_wakeup_host()`` to wake AP

:strong:`Framing` (``cp/db_pack/``): magic validation, sequence increment, CRC, fragment support.

:strong:`CP port binding`: LwIP ephemeral ports (0xc000–0xffff) fall outside CP local range (0x1000–0x1010). CP receive path routes by destination port. Keepalive socket explicitly binds to CP range so peer replies reach CP stack.

4.7 Configuration
,,,,,,,,,,,,,,,,,,,,,


+---------------------+---------------+-----------------------------------------+-------------------------------+
| Parameter           | Default       | Macro                                   | Notes                         |
+=====================+===============+=========================================+===============================+
| Heartbeat interval  | 30 s          | ``DB_KEEPALIVE_DEFAULT_INTERVAL_MS``    | CP RTC wake period            |
+---------------------+---------------+-----------------------------------------+-------------------------------+
| Socket timeout      | 3 s           | ``DB_KEEPALIVE_SOCKET_TIMEOUT_MS``      | TCP connect/recv timeout      |
+---------------------+---------------+-----------------------------------------+-------------------------------+
| Max connect retry   | 5             | ``DB_KEEPALIVE_MAX_RETRY_CNT``          | TCP connect failure           |
+---------------------+---------------+-----------------------------------------+-------------------------------+
| Max no-response     | 3             | ``DB_KEEPALIVE_MAX_NO_RESP_CNT``        | consecutive missed heartbeats |
+---------------------+---------------+-----------------------------------------+-------------------------------+
| Message buffer      | 1460 B        | ``DB_KEEPALIVE_MSG_BUFFER_SIZE``        | max single packet             |
+---------------------+---------------+-----------------------------------------+-------------------------------+
| RTC min interval    | 500 ms        | ``DB_KEEPALIVE_RTC_TIMER_THRESHOLD_MS`` |                               |
+---------------------+---------------+-----------------------------------------+-------------------------------+
| Idle check interval | 10 s          | ``MM_STATUS_CHECK_INTERVAL_MS``         | AP side, CLI configurable     |
+---------------------+---------------+-----------------------------------------+-------------------------------+
| Idle check minimum  | 3 s           | ``MM_STATUS_CHECK_MIN_INTERVAL_MS``     | CLI lower bound               |
+---------------------+---------------+-----------------------------------------+-------------------------------+
| CP bind ports       | 0x1000–0x1010 | ``DB_KEEPALIVE_CP_BIND_PORT_START/END`` | 17 ports                      |
+---------------------+---------------+-----------------------------------------+-------------------------------+


5 API Reference
-------------------


5.1 AP side (``components/smart_lock/``)
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,


:strong:`Keepalive management` (``doorbell_keepalive.h``):

.. code-block:: c

   void doorbell_keepalive_handle_wakeup_reason(void);
   void doorbell_keepalive_on_service_start_success(void);
   void doorbell_keepalive_on_keepalive_disconnection(void);
   bk_err_t doorbell_keepalive_start_mm_status_check(void);
   bk_err_t doorbell_keepalive_stop_mm_status_check(void);
   int doorbell_keepalive_cli_init(void);



:strong:`IPC` (``doorbell_ipc_msg.h``):

.. code-block:: c

   bk_err_t doorbell_ipc_start_keepalive(const char *ip_addr, const char *cmd_port);
   bk_err_t doorbell_ipc_stop_keepalive(void);
   int doorbell_ipc_wakeup_env_init(void);



IPC commands:

+------------------------------------------------+--------+-----------+
| Command                                        | Value  | Direction |
+================================================+========+===========+
| ``DOORBELL_IPC_CMD_KEEPALIVESTART``            | 0x0002 | AP → CP   |
+------------------------------------------------+--------+-----------+
| ``DOORBELL_IPC_CMD_KEEPALIVESTOP``             | 0x0003 | AP → CP   |
+------------------------------------------------+--------+-----------+
| ``DOORBELL_IPC_EVENT_KEEPALIVE_DISCONNECTION`` | 0x1002 | CP → AP   |
+------------------------------------------------+--------+-----------+


5.2 CP side (``cp/keepalive/db_keepalive.h``)
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,


.. code-block:: c

   bk_err_t db_keepalive_cp_init(db_ipc_keepalive_cfg_t *cfg);
   bk_err_t db_keepalive_cp_deinit(void);
   bk_err_t db_keepalive_cp_start(void);
   bk_err_t db_keepalive_cp_stop(void);



5.3 Power control (``cp/powerctrl/powerctrl.h``)
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,


.. code-block:: c

   void pl_wakeup_host(uint32_t flag);
   void pl_power_down_host(void);



6 Layout
------------


.. code-block:: 

   projects/doorbell_lp
   ├── ap/
   │   ├── ap_main.c                 # doorbell config + keepalive init
   │   ├── audio_param/
   │   └── config/bk7259_ap/defconfig
   ├── cp/
   │   ├── cp_main.c
   │   ├── keepalive/                # CP keepalive (TX/RX, RTC, heartbeat)
   │   │   ├── db_keepalive.c
   │   │   └── db_keepalive.h
   │   ├── db_ipc_msg/               # AP↔CP IPC routing
   │   ├── db_pack/                  # framing protocol
   │   └── powerctrl/                # pl_wakeup_host / pl_power_down_host
   ├── partitions/bk7259/
   ├── CMakeLists.txt
   ├── Makefile
   └── dbuild.sh



7 FAQ
---------


:strong:`Q: How to confirm AP is powered down?`

A: No AP-side UART logs (CP logs only). Compare power consumption before/after keepalive.

:strong:`Q: Can BLE provision while in keepalive?`

A: No. BLE runs on AP. After wake, AP reboots — re-provision or restore Wi-Fi config from Flash.

:strong:`Q: Idle check interval vs heartbeat interval?`

A: Idle check (default 10 s, AP) decides when to enter keepalive; heartbeat (30 s, CP) maintains TCP. Both are independently configurable via code/CLI.

:strong:`Q: Keepalive with CS2 P2P?`

A: Not supported. Auto-switches to TCP/UDP before keepalive. Use TCP service mode in production.