# Beken BK7259 Doorbell Solution

- [中文](./README_CN.md)

## Overview

The **BK7259 Doorbell Solution** is an open-source smart video doorbell solution released by Beken, targeting product forms such as video doorbells, network cameras, and two-way video intercom. Built on the **BK7259** host chip and depending on the Armino base SDK **BK_AVDK_SMP**, it provides complete end-to-end example projects covering everything from camera capture and local display to network streaming and two-way intercom, spanning MIPI/UVC camera capture, H.264 and JPEG hardware encoding/decoding, MIPI LCD display, two-way audio with AEC echo cancellation, local voice wake-up, real-time Wi-Fi video streaming, BLE provisioning, two-way video intercom, and AP power-down low-power keepalive, among other capabilities common to doorbell and IPC products.

## Documentation

- [BK7259 Doorbell Solution online documentation](https://docs.bekencorp.com/arminodoc/bk_doorbell/bk7259/en/v4.0.1/index.html)
- [Armino SMP SDK (BK AVDK SMP)](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/en/v4.0.1/index.html)

## Hardware

This solution runs on the BK7259 development board, which carries the BK7259 host chip and, paired with a MIPI LCD panel, a MIPI CSI/UVC camera module, and speaker/mic audio components, performs image capture, local display, two-way intercom, and network streaming for doorbell applications. For the board's interfaces and hardware design details, see the development board hardware reference below.

- [Development board hardware materials](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/ap_doc/bk7259/en/v4.0.1/hw-reference/index.html)
- [BK7259 Datasheet](https://docs.bekencorp.com/spec/BK7259/BK7259_Datasheet.pdf)

Development kit purchase link: coming soon.

## Version Strategy

This solution uses a "maintenance branch + release tag" version management model:

- `release/v4.0.1` is the ongoing maintenance branch for feature iteration and bug fixes of this version series. It always contains the latest code, which may include changes that have not yet completed the full release test.
- `release/v4.0.1.x` are formal release tags, for example `release/v4.0.1.4` and `release/v4.0.1.6`. Each tag has passed the complete test and release process and can be used for mass production.
- The BK7259 Doorbell Solution and the Armino SMP SDK must use exactly the same version tag. For example, when the solution code uses `release/v4.0.1.6`, the SDK must also use `release/v4.0.1.6`.

Customers are advised to choose the tag with the highest version number in the `release/v4.0.1.x` series for development and mass production. Use the `release/v4.0.1` branch only when you need to try the latest features or participate in development.

## Get the Code

Both the BK7259 Doorbell Solution and the Armino SMP SDK are published simultaneously to GitHub, Gitee, and GitLab. Choose a code source based on your network environment and access permissions.

BK7259 Doorbell Solution:

- GitHub: <https://github.com/bekencorp/bk_solution_doorbell>
- Gitee: <https://gitee.com/bekencorp/bk_solution_doorbell>
- GitLab: <https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_doorbell>

Armino SMP SDK:

- GitHub: <https://github.com/bekencorp/bk_avdk_smp>
- Gitee: <https://gitee.com/bekencorp/bk_avdk_smp>
- GitLab: <https://gitlab.bekencorp.com/armino/bk_avdk_smp>

GitHub and Gitee are publicly accessible. GitLab is open to enterprise customers only; enterprise customers who need access should contact their FAE or sales representative to request permission.

**Note for Windows users:** When cloning with Git for Windows, it is recommended to disable automatic newline conversion before cloning, to avoid scripts or source files being converted to CRLF and causing build failures. This is not required on Linux, macOS, or WSL.

```bash
git config --global core.autocrlf false
```

If the code has already been cloned, changing this setting will not restore the files automatically; it is recommended to re-clone after setting it.

The following commands use GitHub and the `release/v4.0.1.6` tag as an example. **When actually getting the code, please choose the latest released tag and ensure the solution code and the SDK use exactly the same tag.** When using another code source, simply replace the corresponding repository address.

```bash
mkdir -p ~/armino && cd ~/armino

# Armino SMP SDK
git clone --branch release/v4.0.1.6 https://github.com/bekencorp/bk_avdk_smp.git

# BK7259 Doorbell Solution
git clone --branch release/v4.0.1.6 https://github.com/bekencorp/bk_solution_doorbell.git
```

For more details on getting the code, see [Armino SMP Quick Start](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/en/v4.0.1/get-started/index.html).

## Set Up the Build Environment

Armino SMP supports both local builds and Docker builds. Choose one of the following methods to set up the build environment based on your development platform.

### Local Build on Linux

Enter the SDK directory and run the environment setup script:

```bash
# The setup script is located inside the bk_avdk_smp repository
cd ~/armino/bk_avdk_smp
sudo bash tools/env_tools/setup/armino_env_setup.sh
```

### Local Build on Windows

Download and install [Armino Bash](https://dl.bekencorp.com/tools/arminosdk/WindowsInstaller/Armino-Bash-Setup_0.3.0.exe).

### Docker Build

The Docker build image is [`bekencorp/armino-idk`](https://hub.docker.com/r/bekencorp/armino-idk/tags). Please choose an image tag of `1.5` or higher, which supports Windows / Linux / macOS.

For detailed setup steps, see [Armino SMP Quick Start: Environment deployment and build](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/en/v4.0.1/get-started/index.html).

## Build the Project

Taking the `doorbell` project as an example (located at `projects/doorbell`), build locally after pointing `SDK_DIR` to the Armino SMP SDK:

```bash
cd ~/armino/bk_solution_doorbell/projects/doorbell
make bk7259 SDK_DIR=~/armino/bk_avdk_smp PROJECT=doorbell
```

Docker builds are also supported (use `./dbuild.sh` on Linux / macOS, or `.\dbuild.ps1` in Windows PowerShell):

```bash
cd ~/armino/bk_solution_doorbell/projects/doorbell
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make bk7259 PROJECT=doorbell
```

After a successful build, the firmware file used for flashing is located at the following path (relative to the `bk_solution_doorbell/` repository root):

```text
projects/doorbell/build/bk7259/doorbell/package/all-app.bin
```

For other projects, replace the path and the `PROJECT` parameter in the commands with the corresponding `projects/<project name>`.

## Flash the Firmware

You can flash the firmware using either of the following methods:

- Download and use the [BKFIL local flashing tool](https://dl.bekencorp.com/tools/bkfil/v4)
- Use the [BKFIL web flashing tool](https://connect.aclsemi.com/)

When flashing, select the `all-app.bin` firmware file generated in the previous section.

For the detailed flashing process, see [Armino SMP Quick Start](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/en/v4.0.1/get-started/index.html).

## Reference Projects

| Project | Main features | Details |
| --- | --- | --- |
| [doorbell](../projects/doorbell/) | Video doorbell: camera capture, H.264 network streaming, LCD display, two-way audio intercom, BLE provisioning. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_doorbell/bk7259/en/v4.0.1/projects/doorbell/index.html) |
| [doorbell_lp](../projects/doorbell_lp/) | Adds low-power keepalive after multimedia goes idle on top of doorbell. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_doorbell/bk7259/en/v4.0.1/projects/doorbell_lp/index.html) |
| [doorbell_lp_QN128B832](../projects/doorbell_lp_QN128B832/) | BK7259QN128B832 (B-package) adaptation of doorbell_lp, with the same class of features. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_doorbell/bk7259/en/v4.0.1/projects/doorbell_lp_QN128B832/index.html) |
| [video_intercom](../projects/video_intercom/) | Two-way video intercom: JSON-RPC control channel, LCD main view shows remote downlink video with a local PIP self-view, plus full-duplex audio. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_doorbell/bk7259/en/v4.0.1/projects/video_intercom/index.html) |

For the applicable scenarios and differences of each project, see the [example projects](https://docs.bekencorp.com/arminodoc/bk_doorbell/bk7259/en/v4.0.1/projects/index.html) page in the online documentation.

## Beken Resources

- [Beken official website](https://www.bekencorp.com/)
- [Armino developer forum](https://armino.bekencorp.com/)
- [Beken documentation center](https://docs.bekencorp.com/)
- WeChat Channels: Beken Corporation
