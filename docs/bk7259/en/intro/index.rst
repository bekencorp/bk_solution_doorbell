BK7259 Doorbell Solution Introduction
===========================================

:link_to_translation:`zh_CN:[中文]`

Overview
---------------------------------

The BK7259 Doorbell Solution is a reference design from Beken built on the BK7259 SoC and the Armino SMP (BK AVDK SMP) v4.0.x architecture. It provides complete end-to-end example projects covering camera capture, local display, network streaming, and two-way intercom:

- MIPI / UVC camera capture with H.264 and JPEG hardware codecs;
- MIPI LCD display;
- Two-way audio with AEC and local voice wake-up;
- Real-time Wi-Fi streaming, BLE provisioning, and two-way video intercom;
- AP power-down low-power keepalive.

The solution runs on the BK7259 development board with a MIPI LCD, MIPI CSI / UVC camera, and speaker / mic. Development-board hardware materials are in :doc:`../hw-reference/index`.

Default resolution, sensor, and panel of each project are defined in that project's documentation and ``defconfig``. Do not treat one board or one panel as the only configuration of the whole solution.

.. note::

   For environment setup, source download, build, and flash, see :doc:`../get-started/index`.
   For companion software download and usage, see :doc:`../software/index`.
   For features and usage of each reference project, see :doc:`../projects/index`.

Design Philosophy
---------------------------------

The BK7259 Doorbell Solution delivers doorbell and intercom applications on top of Armino SMP:

- **BK7259 SoC**: capture, local display, streaming, two-way audio, BLE provisioning, and doorbell logic;
- **Armino SMP**: SoC support, RTOS, drivers, and network stacks;
- **This repository**: solution and business code (``components/``, ``projects/``), mounted at the repo root.

Core Features
---------------------------------

1. Capture and display
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- MIPI CSI / UVC camera capture;
- H.264 and JPEG hardware codecs;
- MIPI LCD local preview (projects with a panel).

2. Streaming and intercom
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- Real-time Wi-Fi streaming;
- Two-way audio with AEC;
- Two-way video intercom (``video_intercom``).

3. Wireless and low power
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- BLE provisioning;
- Local voice wake-up;
- AP power-down keepalive (``doorbell_lp`` and the B-package adaptation).

System Architecture
---------------------------------

.. rubric:: Relationship between the BK7259 Doorbell Solution and BK AVDK SMP

The doorbell solution and the underlying BK AVDK SMP (Armino SMP SDK v4.0.x) split the work as follows:

#. **Scope**: this repo is a scenario-specific solution focused on doorbell capture, streaming, intercom, and provisioning; SoC, RTOS, drivers, and network stacks live in BK AVDK SMP.
#. **Code boundary**: this repo (``bk_solution_doorbell``) provides solution and business code; HW drivers, RTOS, memory management, and Wi-Fi / BLE stacks live in SMP.

On BK7259, Armino SMP follows an AP (application processor) + CP (communication processor) split:

- **AP**: capture, encode, LCD, two-way audio, and doorbell logic;
- **CP**: Wi-Fi, BLE, networking stacks, and low-power management.

Software stack (illustrative)::

    Application (doorbell / intercom / provisioning)
            ↓
    Multimedia (capture / encode / display / audio)
            ↓
    OS (RTOS)
            ↓
    Hardware (BK7259 AP+CP)
