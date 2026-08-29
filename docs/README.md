* [中文](./README_CN.md)

# Doorbell Demo

## Overview

This is a doorbell solution that supports real-time audio and video transmission, and real-time LCD display of UVC camera images.

## Project Compilation

- This project depends on `bk_avdk_smp`. You need to download the `bk_avdk_smp` code when compiling.
- Compilation method: compile under the doorbell solution directory `./projects/doorbell` (same for the doorviewer solution).
- Before compilation, edit the Makefile (`./projects/doorbell/Makefile`) to map the dependent source code to `bk_avdk_smp`, as shown below:

```makefile
# Map dependent source code to bk_avdk_smp
SDK_DIR ?= $(abspath ../..)

# change to
SDK_DIR = /home/user.name/bk_avdk_smp
```

- Compilation command: `make bk7258`
- The above is the compilation command for BK7258. After compilation, a bin file is generated at `./projects/doorbell/build/bk7258/doorbell/package/all-app.bin`.
- Flash this firmware to BK7258.

## Project Demonstration

1. Download the IOT APK from the Beken official website: <https://dl.bekencorp.com/apk/BekenIot.apk>
2. Create your own account and complete login.
3. Add a device and select `Video Doorbell`. DL devices range from 01-18, plus DEBUG. For doorbell, it is recommended to first select `BK7258_DL_01` for trial use; after entering, it details the peripherals used. For doorviewer, it is recommended to select `BK7258_DL_18` for trial use.
4. `Start Adding`, select non-5G Wi-Fi. After a successful connection, click next to start network configuration via Bluetooth.
5. Check the scanned device Bluetooth broadcasts, click on the one with the matching IP address to connect. It will automatically complete 100% of network configuration.
6. After network configuration is complete, the UVC camera automatically opens and network image transmission starts. For doorbell the format is H.264; for doorviewer the format is MJPEG. The image resolution is 864x480.
7. The LCD screen and onboard audio are not displayed initially. You can turn them on and off through buttons in the mobile APK.

> Note: before using these peripherals, ensure the hardware environment is set up properly: the UVC camera is plugged into the BK7258 USB interface, the LCD display is connected, and the audio-related Speaker and Mic are connected. Otherwise, testing related functions may fail or cannot be demonstrated.
