Doorbell Demo
-------------------

Overview
-------------------

    This is a doorbell solution that supports real-time audio and video transmission, and real-time LCD display of UVC camera images.

Project Compilation
-------------------

- This project depends on: ``bk_avdk_smp_main``. You need to download the ``bk_avdk_smp_main`` code when compiling.
- Compilation method: Compile under the doorbell solution directory: ``./projects/doorbell``. (Same for doorviewer solution)
- Before compilation, you need to modify the Makefile file (./projects/doorbell/Makefile) to map the dependent source code to bk_avdk_smp_main, as shown below:

.. code-block:: makefile

    # Map dependent source code to bk_avdk_smp_main
    SDK_DIR ?= $(abspath ../..)

    # change to
    SDK_DIR = /home/user.name/bk_avdk_smp_main

- Compilation command: ``make bk7258``
- The above is the compilation command for BK7258. After compilation, a bin file will be generated at path: ``./projects/doorbell/build/bk7258/doorbell/package/all-app.bin``.
- Flash this firmware to BK7258.

Project Demonstration
----------------------

1. Download the IOT APK from Beken official website: <https://dl.bekencorp.com/apk/BekenIot.apk>

2. Create your own account and complete login

3. Add device, select: `Video Doorbell`. DL devices range from 01-18, and DEBUG. For doorbell, it is recommended to first select `BK7258_DL_01` for trial use. After entering, it will detail the peripherals used. For doorviewer, it is recommended to select `BK7258_DL_18` for trial use

4. `Start Adding`, select non-5G WiFi. After successful connection, click next to start network configuration via Bluetooth

5. Check the scanned device Bluetooth broadcasts, click on the one with matching IP address to connect. It will automatically complete 100% network configuration

6. After network configuration is complete, UVC camera will automatically open and network image transmission will start. For doorbell, the transmission format is H.264; for doorviewer, the transmission format is MJPEG. The image resolution is 864x480

7. LCD screen and onboard audio are not displayed initially. You can choose to turn them on and off through buttons on the mobile APK

.. note::

    Note that before using these peripherals, ensure the hardware environment is set up properly: UVC camera is plugged into the BK7258 USB interface, LCD display is connected, and audio-related Speaker and Mic are also connected. Otherwise, testing related functions may fail or cannot be demonstrated

