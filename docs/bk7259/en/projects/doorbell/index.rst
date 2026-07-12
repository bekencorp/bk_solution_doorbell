Doorbell Project (BK7259 SMP Solution)
==========================================


:link_to_translation:`zh_CN:[中文]`

1 Overview
--------------


This project runs on the BK7259 SMP dual-core platform and provides full video doorbell capabilities: UVC/MIPI camera capture, H.264 encoding, real-time Wi-Fi A/V transmission, BLE provisioning, full-duplex audio, and ASR keyword wake-up.

CPU split:

- :strong:`CP` (M52): ``bk_init()`` brings up the Wi-Fi stack, BLE controller, RF calibration (``vnd_cal``) and CIF low-level AP↔CP link; then ``bk_start_ap_system()`` boots the AP and keeps running wireless control (Wi-Fi/BLE coexistence).
- :strong:`AP` (M55): runs multimedia and doorbell business (``media_service``, ``devices_mgmt``, ``smart_lock`` doorbell core/boarding/network, BLE host, ASR, etc.).

2 Features
--------------


+-----------+--------------------------------------------------------------------------------------+
| Module    | Description                                                                          |
+===========+======================================================================================+
| Camera    | MIPI CSI (default GC2053, 1920×1080@25fps, NV12, Flexa) + UVC (enabled in defconfig) |
+-----------+--------------------------------------------------------------------------------------+
| Display   | MIPI DSI (default ``lcd_device_hx8399c_mipi_1080x1920``), DPU video channel ARGB8888 |
+-----------+--------------------------------------------------------------------------------------+
| GPU       | VG-Lite Flexa 90° rotate / format convert / compress                                 |
+-----------+--------------------------------------------------------------------------------------+
| Codec     | H.264 HW encoder, SW/HW JPEG decoder                                                 |
+-----------+--------------------------------------------------------------------------------------+
| Network   | LAN UDP/TCP, CS2 P2P, real-time Wi-Fi streaming                                      |
+-----------+--------------------------------------------------------------------------------------+
| Bluetooth | BLE Boarding provisioning, BLE Slave-only                                            |
+-----------+--------------------------------------------------------------------------------------+
| Audio     | ADK + AEC v3 + G.711/G.722, on-board Mic/Speaker, UVC UAC                            |
+-----------+--------------------------------------------------------------------------------------+
| ASR       | KWS + TFLite-Micro + NPU                                                             |
+-----------+--------------------------------------------------------------------------------------+
| Snapshot  | SD-card JPEG snapshot (``CONFIG_MDS_SNAPSHOT``)                                      |
+-----------+--------------------------------------------------------------------------------------+


Default board config: ``ap/ap_main.c``. Full Kconfig: ``ap/config/bk7259_ap/defconfig``.

3 Build
-----------


The project depends on the AVDK SDK (``avdk_sdk``). Place the SDK as a sibling directory or set ``SDK_DIR``. In solution mode, use the ``dbuild.sh`` wrapper (injects ``BK_SOLUTION_MODE=1``, ``SOLUTION_DIR``, ``PROJECT_DIR``):

.. code-block:: bash

   cd projects/doorbell
   ./dbuild.sh make bk7259 PROJECT=doorbell



Pin the SDK path:

.. code-block:: bash

   SDK_DIR=/abs/path/to/avdk_sdk ./dbuild.sh make bk7259 PROJECT=doorbell



Build output:

.. code-block:: 

   projects/doorbell/build/bk7259/doorbell/package/all-app.bin



Flash this bin to BK7259v2.

4 Flash and Demo
--------------------


1. Install BekenIot APK: <https://dl.bekencorp.com/apk/BekenIot.apk>
2. Sign up / log in
3. Add device → ``Video Doorbell``
4. Pick non-5G Wi-Fi, BLE provisioning
5. Tap the device with matching IP in the scan list
6. Camera and H.264 stream start automatically after provisioning (1920×1080)
7. Toggle on-board LCD and audio from APK buttons

> Connect MIPI camera, MIPI LCD, speaker and mic before testing. Plug UVC camera into USB if used.

5 Layout
------------


.. code-block:: 

   projects/doorbell
   ├── ap/                       # AP business (doorbell + media + ASR)
   │   ├── ap_main.c             # board camera/display/gpu config
   │   ├── CMakeLists.txt
   │   ├── audio_param/
   │   └── config/bk7259_ap/defconfig
   ├── cp/                       # CP (M52): Wi-Fi/BLE Controller, vnd_cal, bk_start_ap_system()
   │   ├── cp_main.c
   │   ├── CMakeLists.txt
   │   └── config/bk7259/defconfig
   ├── partitions/bk7259/        # flash partitions and RAM regions
   │   ├── auto_partitions.csv
   │   └── ram_regions.csv
   ├── CMakeLists.txt            # top-level; injects ../../components as EXTRA_COMPONENTS_DIRS
   ├── Makefile
   ├── dbuild.sh / dbuild.ps1    # solution-mode docker build wrapper
   └── .ci                       # CI build command



Shared solution components under ``components/``:

- ``multimedia_device_service``: camera/display/gpu/codec/uvc device management
- ``smart_lock``: doorbell_core, ble_boarding, network_transfer, audio_device, cmd, etc.
- ``lcd_device``: ST7701SN RGB panel driver (opt-in)