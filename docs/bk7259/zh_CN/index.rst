博通集成 Armino doorbell 解决方案（BK7259）
==============================================

:link_to_translation:`en:[English]`

本方案基于 :strong:`BK7259 SMP 双核架构`，提供可视门铃、低功耗保活门铃、无屏 IPC 及 ISP/H264E 联调等工程。

示例工程
--------

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - 工程
     - 说明
   * - doorbell
     - 可视门铃：MIPI/UVC 采集、H.264 图传、MIPI LCD、BLE 配网
   * - doorbell_lp
     - 在 doorbell 基础上增加低功耗保活能力
   * - ipc
     - 无屏 IPC：SC3336 2304×1296，共用 doorbell 业务栈
   * - isp_h264_tuning
     - ISP / H264E PC 端图像质量联调（Wi-Fi JSON-RPC）

1. 依赖与目录
-------------

Solution 依赖 AVDK SDK（``bk_avdk_smp``）。SDK 需与本 solution 处于同一上级目录，
或通过 ``SDK_DIR`` 显式指定。

2. 编译
-------

Solution 模式推荐使用各工程目录下的 ``dbuild.sh``（自动注入 ``BK_SOLUTION_MODE=1``、``SOLUTION_DIR``、``PROJECT_DIR``）：

.. code-block:: bash

    cd projects/doorbell
    ./dbuild.sh make bk7259 PROJECT=doorbell

显式指定 SDK 路径：

.. code-block:: bash

    cd projects/doorbell
    SDK_DIR=/abs/path/to/bk_avdk_smp ./dbuild.sh make bk7259 PROJECT=doorbell

各工程编译产物路径：

.. code-block:: text

    projects/<name>/build/bk7259/<name>/package/all-app.bin

将 ``all-app.bin`` 烧录到 BK7259v2 即可。

3. 演示
-------

1. 下载 BekenIot APK：<https://dl.bekencorp.com/apk/BekenIot.apk>
2. 注册并登录，添加设备 → ``可视门铃``
3. 选择 2.4G Wi-Fi，通过 BLE 配网
4. 配网完成后自动开启摄像头与 H.264 图传（doorbell 默认 MIPI 1920×1080）
5. LCD 与音频通过 APK 按钮控制

4. 参考文档
-----------

Armino SMP 架构请参考 `Armino SMP 架构 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v4.0.1/index.html>`_。

应用处理器 AP 配置和使用请参考 `Armino AP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/ap_doc/bk7259/zh_CN/v4.0.1/index.html>`_。

通信处理器 CP 配置和使用请参考 `Armino CP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/cp_doc/bk7259/zh_CN/v4.0.1/index.html>`_。

.. toctree::
    :hidden:

    Armino SMP 架构 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v4.0.1/index.html>
    示例工程 <projects/index>

* :ref:`genindex`
