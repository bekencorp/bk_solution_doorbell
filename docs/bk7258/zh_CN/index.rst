博通集成 Armino doorbell解决方案
=====================================

:link_to_translation:`en:[English]`

这是博通集成 Armino doorbell解决方案的官方文档。

Armino doorbell解决方案基于Armino SMP架构, 帮助用户开发应用;
代码下载及版本编译方法如下:

1. Armino SDK 代码下载
------------------------------------

您可从 gitlab 上下载 Armino SMP 代码::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/bk_avdk_smp.git -b release/v3.1.1

2. Armino doorbell解决方案 代码下载
------------------------------------

您可从 gitlab 上下载 Armino doorbell解决方案 代码::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_doorbell.git -b release/v3.1.1


3. Armino doorbell解决方案 版本编译
------------------------------------

版本编译方法如下::

    cd ~/armino/bk_solution_doorbell
    cd projects/doorbell
    make clean SDK_DIR=~/armino/bk_avdk_smp
    make bk7258 SDK_DIR=~/armino/bk_avdk_smp

4. Armino doorviewer解决方案 版本编译
--------------------------------------

版本编译方法如下::

    cd ~/armino/bk_solution_doorbell
    cd projects/doorviewer
    make clean SDK_DIR=~/armino/bk_avdk_smp
    make bk7258 SDK_DIR=~/armino/bk_avdk_smp

或者可以通过export来指定SDK路径::

    cd ~/armino/bk_solution_doorbell
    cd projects/doorviewer
    export SDK_DIR=~/armino/bk_avdk_smp
    make clean
    make bk7258

使用docker编译方式如下

    linux或macos系统下在终端执行以下命令::

        export SDK_DIR=~/armino/bk_avdk_smp
        ./dbuild.sh make clean
        ./dbuild.sh make bk7258

    Windows下使用powershell执行以下命令::

        $env:SDK_DIR = "C:\armino\bk_avdk_smp"
        ./dbuild.ps1 make clean
        ./dbuild.ps1 make bk7258

本文档基于Armino SMP架构, 帮助用户开发应用;

Armino SMP架构, 请参考 `Armino SMP架构 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/index.html>`_

应用处理器AP配置和使用, 请参考 `Armino AP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/ap_doc/bk7258/zh_CN/v3.1.1/index.html>`_

通信处理器CP配置和使用, 请参考 `Armino CP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/cp_doc/bk7258/zh_CN/v3.1.1/index.html>`_

.. toctree::
    :hidden:

    Armino SMP架构 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/index.html>

    示例工程 <projects/index>

* :ref:`genindex`



