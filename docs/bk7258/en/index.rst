Beken Armino Doorbell Solution
=====================================

:link_to_translation:`zh_CN:[中文]`

This is the official documentation for Beken Armino doorbell solution.

Armino doorbell solution is based on Armino SMP architecture to help users develop applications.
Code download and version compilation methods are as follows:

1. Armino SDK Code Download
------------------------------------

You can download the Armino SMP code from gitlab::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/bk_avdk_smp.git -b release/v3.1.1

2. Armino Doorbell Solution Code Download
--------------------------------------------

You can download the Armino doorbell solution code from gitlab::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_doorbell.git -b release/v3.1.1


3. Armino Doorbell Solution Version Compilation
----------------------------------------------------

The version compilation method is as follows::

    cd ~/armino/bk_solution_doorbell
    cd projects/doorbell
    make clean SDK_DIR=~/armino/bk_avdk_smp
    make bk7258 SDK_DIR=~/armino/bk_avdk_smp

4. Armino Doorviewer Solution Version Compilation
------------------------------------------------------

The version compilation method is as follows::

    cd ~/armino/bk_solution_doorbell
    cd projects/doorviewer
    make clean SDK_DIR=~/armino/bk_avdk_smp
    make bk7258 SDK_DIR=~/armino/bk_avdk_smp

Alternatively, you can specify the SDK path using the export command::

    cd ~/armino/bk_solution_doorbell
    cd projects/doorviewer
    export SDK_DIR=~/armino/bk_avdk_smp
    make clean
    make bk7258

Compile using Docker as follows::

    On Linux or macOS systems, execute the following commands in the terminal::

        export SDK_DIR=~/armino/bk_avdk_smp
        ./dbuild.sh make clean
        ./dbuild.sh make bk7258

    On Windows, use PowerShell to execute the following commands::

        $env:SDK_DIR = "C:\armino\bk_avdk_smp"
        ./dbuild.ps1 make clean
        ./dbuild.ps1 make bk7258

This documentation is based on Armino SMP architecture to help users develop applications.

For Armino SMP architecture, please refer to `Armino SMP Architecture <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.0.1/index.html>`_

For Application Processor (AP) configuration and usage, please refer to `Armino AP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/ap_doc/bk7258/zh_CN/v3.0.1/index.html>`_

For Communication Processor (CP) configuration and usage, please refer to `Armino CP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/cp_doc/bk7258/zh_CN/v3.0.1/index.html>`_

.. toctree::
    :hidden:

    Armino SMP Architecture <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.0.1/index.html>

    Example Projects <projects/index>

* :ref:`genindex`



