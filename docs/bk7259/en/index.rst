Beken Armino Doorbell Solution (BK7259)
========================================

:link_to_translation:`zh_CN:[中文]`

This solution targets the BK7259 SMP dual-core platform (CP: M52, AP: M55) and ships doorbell, low-power keepalive, headless IPC, and ISP/H264E tuning projects.

Example Projects
----------------

+-------------------+----------------------------------------------------------+
| Project           | Description                                              |
+===================+==========================================================+
| doorbell          | Video doorbell: MIPI/UVC capture, H.264 stream, MIPI LCD |
+-------------------+----------------------------------------------------------+
| doorbell_lp       | doorbell + AP power-down / CP TCP keepalive              |
+-------------------+----------------------------------------------------------+
| ipc               | Headless IPC: SC3336 2304×1296, shared doorbell stack    |
+-------------------+----------------------------------------------------------+
| isp_h264_tuning   | ISP / H264E PC-side tuning over Wi-Fi JSON-RPC           |
+-------------------+----------------------------------------------------------+

1. Dependencies
---------------

The solution depends on the AVDK SDK (``bk_avdk_smp``). Place the SDK as a sibling directory or set ``SDK_DIR``.

2. Build
--------

Use each project's ``dbuild.sh`` wrapper (sets ``BK_SOLUTION_MODE=1``, ``SOLUTION_DIR``, ``PROJECT_DIR``):

.. code-block:: bash

    cd projects/doorbell
    ./dbuild.sh make bk7259 PROJECT=doorbell

Pin the SDK path:

.. code-block:: bash

    cd projects/doorbell
    SDK_DIR=/abs/path/to/bk_avdk_smp ./dbuild.sh make bk7259 PROJECT=doorbell

Firmware output:

.. code-block:: text

    projects/<name>/build/bk7259/<name>/package/all-app.bin

Flash ``all-app.bin`` to BK7259v2.

3. Demo
-------

1. Install BekenIot APK: <https://dl.bekencorp.com/apk/BekenIot.apk>
2. Sign up / log in, add device → ``Video Doorbell``
3. Pick 2.4G Wi-Fi, BLE provisioning
4. Camera + H.264 stream starts after provisioning (doorbell default MIPI 1920×1080)
5. Toggle LCD and audio from APK buttons

4. References
-------------

For Armino SMP architecture, see `Armino SMP Architecture <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/en/v4.0.1/index.html>`_.

For Application Processor (AP) configuration and usage, see `Armino AP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/ap_doc/bk7259/en/v4.0.1/index.html>`_.

For Communication Processor (CP) configuration and usage, see `Armino CP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/cp_doc/bk7259/en/v4.0.1/index.html>`_.

.. toctree::
    :hidden:

    Armino SMP Architecture <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/en/v4.0.1/index.html>
    Example Projects <projects/index>

* :ref:`genindex`
