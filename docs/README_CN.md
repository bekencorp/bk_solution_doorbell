* [English](./README.md)

# doorbell demo

## 概述

此为 doorbell 解决方案，支持实时音视频传输，和 uvc 摄像头的图像实时 LCD 显示。

## 工程编译

- 此工程依赖 `bk_avdk_smp_main`。编译时需要下载 `bk_avdk_smp_main` 代码。
- 编译方法：在 doorbell 方案目录 `./projects/doorbell` 下编译（doorviewer 方案一样）。
- 编译之前需要先修改 Makefile 文件（`./projects/doorbell/Makefile`），将依赖的源码映射到 `bk_avdk_smp_main` 上，参考如下：

```makefile
# 映射依赖的源码到 bk_avdk_smp_main 上
SDK_DIR ?= $(abspath ../..)

# change to
SDK_DIR = /home/user.name/bk_avdk_smp_main
```

- 编译命令：`make bk7258`
- 上面是 BK7258 的编译命令，编译完成后会生成 bin 文件，路径：`./projects/doorbell/build/bk7258/doorbell/package/all-app.bin`。
- 烧录此固件到 BK7258 上。

## 工程演示

1. 从博通集成官网地址下载 IOT APK 进行使用：<https://dl.bekencorp.com/apk/BekenIot.apk>
2. 自行创建账号，并完成登录。
3. 添加设备，选择 `可视门铃`。DL 设备存在 01-18 和 DEBUG，doorbell 建议先选择 `BK7258_DL_01` 进行尝试使用；点进去后里面详细介绍了使用的外设。doorviewer 建议选择 `BK7258_DL_18` 进行尝试使用。
4. `开始添加`，选择非 5G 的 WiFi，连接成功后点击下一步，开始通过蓝牙进行配网。
5. 检查扫描到的设备蓝牙广播，点击 IP 地址匹配的进行连接，会自动完成 100% 的配网。
6. 配网完成之后会自动打开 UVC 摄像头，且打开网络图传：doorbell 传输的格式是 H.264，doorviewer 传输的格式是 MJPEG，图像的分辨率为 864X480。
7. LCD 屏幕上暂未显示，板载语音也未显示，通过手机 APK 上的按钮可以选择打开和关闭。

> 注意：使用这些外设之前，硬件环境需已搭建好——UVC 已插入到 BK7258 的 USB 接口上，LCD 显示屏也已接上，语音相关的 Speaker 和 Mic 也已接上，否则测试相关功能会失败或者无法展示。
