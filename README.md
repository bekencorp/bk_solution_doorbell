# Beken BK7258 Doorbell Solution

- [中文](./README_CN.md)

## Overview

The **BK7258 Doorbell Solution** is an open-source audio/video solution for video doorbells and smart door locks released by Beken. It is based on the **BK7258** host chip and depends on the Armino base SDK **BK_AVDK_SMP**, providing complete end-to-end example projects covering camera capture, local display, network video streaming, and two-way intercom, including UVC/DVP camera capture, MJPEG decoding and H.264 encoding, LCD display with LVGL, two-way audio with AEC echo cancellation, Wi-Fi video streaming, BLE provisioning, and AP power-off low-power keep-alive, which are common capabilities for doorbell and door-lock devices.

## Documentation

- [BK7258 Doorbell Solution online documentation](https://docs.bekencorp.com/arminodoc/bk_doorbell/bk7258/en/v3.1.1/index.html)
- [Armino SMP SDK (BK AVDK SMP)](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/index.html)

## Hardware

This solution runs on a BK7258 development board. Together with an LCD panel, a UVC/DVP camera module, and an onboard Speaker/Mic or UAC audio device, the board can perform image capture, display, and two-way audio for doorbell and door-lock applications.

- [Development board hardware materials](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/ap_doc/bk7258/en/v3.1.1/hw-reference/index.html)
- [BK7258 Datasheet](https://docs.bekencorp.com/spec/BK7258/BK7258%C2%A0Datasheet.pdf)

[BK7258 purchase link](https://item.taobao.com/item.htm?id=795814346530)

## Version Strategy

This solution uses a "maintenance branch + release tag" version management approach:

- `release/v3.1.1` is the continuous maintenance branch, used for feature iteration and bug fixing of this version series. It always contains the latest code, which may include changes that have not yet completed the full release test process.
- `release/v3.1.1.x` are the official release tags, for example `release/v3.1.1.2` and `release/v3.1.1.4`. Each tag has gone through the complete test and release process and can be used for mass production.
- The BK7258 Doorbell Solution and the Armino SMP SDK must use exactly the same version tag. For example, when the solution code uses `release/v3.1.1.8`, the SDK must also use `release/v3.1.1.8`.

We recommend that customers choose the tag with the highest version number in the `release/v3.1.1.x` series for development and mass production. Only use the `release/v3.1.1` branch when you need to try out the latest features or participate in development.

## Get the Code

Both the BK7258 Doorbell Solution and the Armino SMP SDK are published simultaneously to GitHub, Gitee, and GitLab. Choose a code source according to your network environment and access permissions.

BK7258 Doorbell Solution:

- GitHub: <https://github.com/bekencorp/bk_solution_doorbell>
- Gitee: <https://gitee.com/bekencorp/bk_solution_doorbell>
- GitLab: <https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_doorbell>

Armino SMP SDK:

- GitHub: <https://github.com/bekencorp/bk_avdk_smp>
- Gitee: <https://gitee.com/bekencorp/bk_avdk_smp>
- GitLab: <https://gitlab.bekencorp.com/armino/bk_avdk_smp>

GitHub and Gitee are publicly accessible. GitLab is only open to enterprise customers; enterprise customers who need access should contact their FAE or sales representative to apply for permission.

**Note for Windows users:** When getting the code with Git for Windows, disable automatic line-ending conversion before cloning to avoid scripts or source files being converted to CRLF and causing build failures. This is not required on Linux, macOS, or WSL.

```bash
git config --global core.autocrlf false
```

If the code has already been cloned, changing this setting does not automatically restore the files, so it is recommended to re-clone after configuring it.

The following commands use GitHub and the `release/v3.1.1.8` tag as an example. **When actually getting the code, please choose the latest released tag and make sure the solution code and the SDK use exactly the same tag.** Replace the repository address accordingly when using another code source.

```bash
mkdir -p ~/armino && cd ~/armino

# Armino SMP SDK
git clone --branch release/v3.1.1.8 https://github.com/bekencorp/bk_avdk_smp.git

# BK7258 Doorbell Solution
git clone --branch release/v3.1.1.8 https://github.com/bekencorp/bk_solution_doorbell.git
```

For more details on getting the code, see [Armino SMP Quick Start](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/get-started/index.html).

## Set Up the Build Environment

Armino SMP supports both local build and Docker build. Choose one of the following ways to set up the build environment according to your development platform.

### Local Build on Linux

Enter the SDK directory and run the environment setup script:

```bash
# The setup script is located in the bk_avdk_smp repository
cd ~/armino/bk_avdk_smp
sudo bash tools/env_tools/setup/armino_env_setup.sh
```

### Local Build on Windows

Download and install [Armino Bash](https://dl.bekencorp.com/tools/arminosdk/WindowsInstaller/Armino-Bash-Setup_0.3.0.exe).

### Docker Build

The Docker build image is [`bekencorp/armino-idk`](https://hub.docker.com/r/bekencorp/armino-idk/tags). Please choose an image tag of `1.5` or higher. It supports Windows / Linux / macOS.

For detailed setup steps, see [Armino SMP Quick Start: Environment deployment and build](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/get-started/index.html).

## Build the Project

Taking the `doorbell` project as an example (located at `projects/doorbell`), build locally after pointing `SDK_DIR` to the Armino SMP SDK:

```bash
cd ~/armino/bk_solution_doorbell/projects/doorbell
make bk7258 SDK_DIR=~/armino/bk_avdk_smp        # you can also export SDK_DIR first, then run make bk7258
```

Docker build is also supported (use `./dbuild.sh` on Linux / macOS, and `.\dbuild.ps1` in Windows PowerShell):

```bash
cd ~/armino/bk_solution_doorbell/projects/doorbell
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make bk7258
```

After a successful build, the firmware file used for flashing is located at the following path (relative to the `bk_solution_doorbell/` repository root):

```text
projects/doorbell/build/bk7258/doorbell/package/all-app.bin
```

For other projects, replace the path in the commands with the corresponding `projects/<project_name>`.

## Flash the Firmware

You can flash the firmware using any of the following methods:

- Download and use the [BKFIL local flashing tool](https://dl.bekencorp.com/tools/flash/)
- Use the [BKFIL web flashing tool](https://connect.aclsemi.com/)

When flashing, select the `all-app.bin` firmware file generated in the previous section.

For the detailed flashing process, see [Armino SMP Quick Start](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/get-started/index.html).

## Reference Projects

| Project | Main features | Details |
| --- | --- | --- |
| [doorbell](../projects/doorbell/) | H.264 network streaming, LCD display, two-way intercom, BLE provisioning. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_doorbell/bk7258/en/v3.1.1/projects/doorbell/index.html) |
| [doorviewer](../projects/doorviewer/) | MJPEG network streaming and LCD display, with better compatibility. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_doorbell/bk7258/en/v3.1.1/projects/doorviewer/index.html) |
| [doorbell_lp](../projects/doorbell_lp/) | Adds AP power-off low-power keep-alive on top of doorbell. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_doorbell/bk7258/en/v3.1.1/projects/doorbell_lp/index.html) |
| [doorbell_4M](../projects/doorbell_4M/) | MJPEG streaming and LCD display; audio supports onboard Speaker and UAC. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_doorbell/bk7258/en/v3.1.1/projects/doorbell_4M/index.html) |

For the applicable scenarios and differences of each project, see the [example projects](https://docs.bekencorp.com/arminodoc/bk_doorbell/bk7258/en/v3.1.1/projects/index.html) page in the online documentation.

## Beken Resources

- [Beken official website](https://www.bekencorp.com/)
- [Armino developer forum](https://armino.bekencorp.com/)
- [Beken documentation center](https://docs.bekencorp.com/)
- Bilibili: Beken Product Solutions
