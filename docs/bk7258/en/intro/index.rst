BK7258 Doorbell Solution Introduction
===========================================

:link_to_translation:`zh_CN:[中文]`

Overview
---------------------------------

The BK7258 Doorbell Solution is a reference design from Beken built on the BK7258 SoC and the Armino SMP (BK AVDK SMP) v3.1.x architecture. It provides complete end-to-end example projects covering camera capture, local display, network streaming, and two-way intercom:

- UVC / DVP camera capture;
- MJPEG decode and H.264 encode;
- LCD display and LVGL;
- Two-way audio with AEC, Wi-Fi streaming, and BLE provisioning;
- AP power-down low-power keepalive.

The solution runs on the BK7258 development board with an LCD, UVC/DVP camera, and speaker / mic or UAC. Development-board hardware materials are in :doc:`../hw-reference/index`.

Default resolution, sensor, and streaming format of each project are defined in that project's documentation and ``defconfig``. Do not treat one panel or one camera as the only configuration of the whole solution.

.. note::

   For environment setup, source download, build, and flash, see :doc:`../get-started/index`.
   For companion software download and usage, see :doc:`../software/index`.
   For features and usage of each reference project, see :doc:`../projects/index`.

Design Philosophy
---------------------------------

The BK7258 Doorbell Solution delivers doorbell and door-lock A/V applications on top of Armino SMP:

- **BK7258 SoC**: capture, local display, streaming, two-way audio, BLE provisioning, and doorbell logic;
- **Armino SMP**: SoC support, RTOS, drivers, and network stacks;
- **This repository**: solution and business code (``components/``, ``projects/``), mounted at the repo root.

Core Features
---------------------------------

1. Capture and display
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- UVC / DVP capture;
- H.264 and MJPEG streaming paths (per project);
- LCD local preview.

2. Streaming and intercom
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- Wi-Fi streaming;
- Two-way audio with AEC;
- BLE provisioning.

3. Low power
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- AP power-down keepalive (``doorbell_lp``).

System Architecture
---------------------------------

.. rubric:: Relationship between the BK7258 Doorbell Solution and BK AVDK SMP

The doorbell solution and the underlying BK AVDK SMP (Armino SMP SDK v3.1.x) split the work as follows:

#. **Scope**: this repo is a scenario-specific solution focused on doorbell capture, streaming, intercom, and provisioning; SoC, RTOS, drivers, and network stacks live in BK AVDK SMP.
#. **Code boundary**: this repo (``bk_solution_doorbell``) provides solution and business code; HW drivers, RTOS, memory management, and Wi-Fi / BLE stacks live in SMP.

On BK7258, Armino SMP follows an AP (application processor) + CP (communication processor) split:

- **AP**: capture, encode, LCD, two-way audio, and doorbell logic;
- **CP**: Wi-Fi, BLE, networking stacks, and low-power management.
