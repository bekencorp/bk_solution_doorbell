Beken Armino Armino doorbell Solution Development Document
=============================================================

:link_to_translation:`zh_CN:[中文]`

Armino doorbell Solution
------------------------------------


This is the official documentation for Beken Armino doorbell solution.

The Armino doorbell solution is based on the Armino SMP architecture to help users develop applications.
The code download and version compilation methods are as follows:

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
    cd ~/projects/app
    make clean SDK_DIR=~/armino/bk_avdk_smp
    make bk7258 SDK_DIR=~/armino/bk_avdk_smp


This document is based on the Armino SMP architecture to help users develop applications.

For the Armino SMP architecture, please refer to `Armino SMP Architecture <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.0.1/index.html>`_

For Application Processor (AP) configuration and usage, please refer to `Armino AP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/ap_doc/bk7258/zh_CN/v3.0.1/index.html>`_

For Communication Processor (CP) configuration and usage, please refer to `Armino CP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/cp_doc/bk7258/zh_CN/v3.0.1/index.html>`_

.. toctree::
    :hidden:

    Armino SMP Architecture <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.0.1/index.html>

    Example Projects <projects/index>

* :ref:`genindex`



