门锁demo
-------------------

概述
-------------------

    此为门锁解决方案，支持实时音视频传输，和uvc摄像头的图像实时LCD显示。

工程编译
-------------------

- 此工程依赖： ``bk_avdk_smp_main``。编译时需要下载 ``bk_avdk_smp_main`` 代码。
- 编译方法：在两轮车方案下： ``./projects/doorbell``，下编译。
- 编译之前需要先修改Makefile文件（./projects/doorbell/Makefile），将依赖的源码映射到bk_avdk_smp_main上，参考如下：

.. code-block:: makefile

    # 映射依赖的源码到bk_avdk_smp_main上
    SDK_DIR ?= $(abspath ../..)

    # change to
    SDK_DIR = /home/user.name/bk_avdk_smp_main

- 编译命令： ``make bk7258``
- 上面是BK7258的编译命令，编译完成后，会生成bin文件件，路径： ``./projects/doorbell/build/bk7258/doorbell/package/all-app.bin``。
- 烧录此固件到BK7258上。

工程演示
----------------------

1. 从博通集成官网地址下载IOT APK进行使用： <https://dl.bekencorp.com/apk/BekenIot.apk>

2. 自行创建账号，并完成登录

3. 添加设备，选择： `可视门铃` ，DL设备存在01-18，和DEBUG，建议先选择 `BK7258_DL_01` 进行尝试使用。点进去后里面详细介绍了使用的外设

4. `开始添加`，选择非5G的WiFi，连接成功后，点击下一步，开始通过蓝牙进行配网

5. 检查扫描到的设备蓝牙广播，点击IP地址匹配的进行连接，会自动完成100%的配网

6. 配网完成之后会自动打开UVC摄像头，且打开网络图传，传输的格式是H.264，图像的分辨率为864X480

7. LCD屏幕上暂未显示，板载语音也未显示，通过手机APK上的按钮可以选择打开和关闭

.. note::

    注意使用这些外设之前，硬件环境已经搭建好，UVC已经插入到BK7258的USB接口上，LCD显示屏也已经接上，语音相关的Speaker和Mic也已经接上，否则测试相关功能会失败或者无法展示
