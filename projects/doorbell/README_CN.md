# Doorbell项目开发指南

* [English](./README.md)

## 1 项目概述

本项目是一个基于BK7258芯片的智能门铃门锁解决方案，实现了通过WiFi传输图像数据，并在LCD屏幕上显示的功能，实时双向音频的功能。项目集成了丰富的多媒体处理能力、网络通信功能和用户界面显示，适用于智能门铃门锁设备的开发。区别于doorbell工程，支持的是camera的MJPEG图传与LCD显示，

## 2 功能特性

### 2.1 WiFi通信
- 支持STA模式连接到现有WiFi网络
- 支持AP模式创建WiFi热点供BK7258设备连接
- 支持TCP/UDP协议传输图像数据
- 支持CS2实时网络传输

### 2.2 多媒体处理
- 支持UVC摄像头控制与图像采集，默认格式为MJPEG，分辨率为864x480，帧率为30fps
- 支持软件或硬件解码，系统内部自动选择
- 支持H.264编码功能，编码时使用硬编码
- 支持默认三种帧的buffer池管理
- 支持多种LCD屏幕显示，RGB屏或MCU屏，默认使用RBGB屏（st7701sn）
- 支持多种音频编解码算法，默认使用G711编码
- 支持多种传输协议，实时传输音视频数据

### 2.3 显示功能
- LCD屏幕显示
- LVGL图形库支持（可选）
- AVI视频播放（可选）

### 2.4 蓝牙功能
- 蓝牙基础功能
- A2DP音频接收
- HFP免提通话
- BLE功能
- WiFi配网功能

## 3 快速开始

### 3.1 硬件准备
- BK7258开发板
- LCD屏幕
- UVC/DVP摄像头模块
- 板载speaker/mic，或UAC
- 电源和连接线

### 3.2 编译和烧录

编译流程参考 `Doorbell 解决方案 <../../index.html>`_

烧录流程参考 `烧录代码 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.0.1/get-started/index.html#id7>`_

编译生成的烧录bin文件路径：``projects/doorbell/build/bk7258/doorbell/package/all-app.bin``

### 3.3 基本操作流程
1. 设备上电启动
2. 测试机（Android）下载IOT应用到设备，下载地址： <https://dl.bekencorp.com/apk/BekenIot.apk>
3. 自行创建账号，并完成登录
4. 测试机打开IOT应用，添加设备，选择： `可视门铃` ，DL设备存在01-18，和DEBUG，建议先选择 `BK7258_DL_01` 进行尝试使用。点进去后里面详细介绍了使用的外设，包括UVC摄像头、H.264编码器、SD卡存储、LCD屏幕、语音功能等。
5. `开始添加`，选择非5G的WiFi，连接成功后，点击下一步，开始通过蓝牙进行配网
6. 检查扫描到的设备蓝牙广播，点击IP地址匹配的进行连接，会自动完成100%的配网
7. 配网完成之后会自动打开UVC摄像头，且打开网络图传，传输的格式是H.264，图像的分辨率为864X480
8. 打开其他外设，可以在IOT应用上进行控制。

## 4 doorbell视频流方案

本项目支持两种摄像头方案，均采用多消费者帧队列架构，实现高效的视频流处理和分发。

### 4.1 方案一：UVC摄像头 + MJPEG + LCD显示 + H264网络传输

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            UVC Camera (MJPEG输出)                            │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
                    ┌──────────────────────────────┐
                    │  MJPEG Frame Queue (V2)      │
                    │  - 支持多消费者并发访问       │
                    │  - 引用计数自动管理          │
                    │  - consumer_mask: 0x02       │
                    └──────────────┬───────────────┘
                                   │
                                   ▼
                        ┌─────────────────────┐
                        │  Decoder Consumer    │
                        │  (CONSUMER_DECODER)  │
                        │  ID: 0x02           │
                        └──────────┬──────────┘
                                   │
                                   │ MJPEG解码
                                   ▼
                        ┌─────────────────────┐
                        │   YUV Buffer        │
                        │   (直接内存申请)     │
                        │   非队列管理        │
                        └──────────┬──────────┘
                                   │
                    ┌──────────────┴──────────────┐
                    │                             │
                    ▼                             ▼
         ┌─────────────────────┐      ┌────────────────────┐
         │  Video Pipeline      │      │  H264 Encoder      │
         │  视频处理：           │      │  Pipeline          │
         │  1. 旋转(可选)        │      │                    │
         │  2. 缩放(可选)        │      │  输入: YUV原始数据 │
         │  3. 格式转换         │      │  输出: H264编码流  │
         └──────────┬──────────┘      └────────┬───────────┘
                    │                          │
                    │ 处理后图像                │ H264编码
                    │ (直接内存)                │
                    ▼                          ▼
         ┌─────────────────────┐      ┌────────────────────┐
         │  LCD Display        │      │ H264 Frame         │
         │  Driver             │      │ Queue (V2)         │
         │                     │      │ consumer_mask:0x01 │
         └─────────────────────┘      └────────┬───────────┘
                                               │
                                               ▼
                                      ┌────────────────────┐
                                      │  WiFi Transfer     │
                                      │  Consumer (0x01)   │
                                      └────────┬───────────┘
                                               │
                                               ▼
                                      ┌────────────────────┐
                                      │  网络传输           │
                                      │  (CS2/TCP/UDP)     │
                                      └────────┬───────────┘
                                               │
                                               ▼
                                      ┌────────────────────┐
                                      │  对端设备           │
                                      │  解码显示           │
                                      └────────────────────┘
```

**流程说明：**

1. **MJPEG采集与分发**
   - UVC摄像头直接输出MJPEG压缩流（默认864x480@30fps）
   - MJPEG帧通过 `frame_queue_v2_complete()` 放入MJPEG帧队列
   - 解码器注册为消费者（CONSUMER_DECODER），通过 `frame_queue_v2_get_frame()` 获取MJPEG帧

2. **MJPEG解码**
   - 解码器将MJPEG解码为YUV格式
   - **YUV数据直接使用内存申请，不经过YUV Frame Queue**
   - 自动选择硬件或软件解码器
   - 解码后的YUV buffer直接传递给后续模块

3. **本地显示路径**
   - YUV buffer通过视频处理管道进行：

     * 软件/硬件旋转（支持0°/90°/180°/270°）
     * 图像缩放（适配LCD分辨率）
     * 像素格式转换（RGB565/RGB888等）

   - **处理后的图像数据也是直接内存申请**
   - 最终通过LCD驱动显示到屏幕

4. **网络传输路径**
   - 解码后的YUV buffer直接送入H264编码器
   - H264编码器进行硬件编码
   - H264帧通过 `frame_queue_v2_complete()` 放入H264帧队列
   - WiFi传输模块注册为H264消费者（CONSUMER_TRANSMISSION）
   - 通过 `frame_queue_v2_get_frame()` 获取H264帧
   - 通过CS2/TCP/UDP协议传输到对端设备

5. **内存管理说明**
   - **MJPEG Frame Queue**：使用V2队列管理，支持多消费者
   - **YUV Buffer**：解码模块直接申请和释放内存，不使用队列
   - **处理后图像**：视频处理管道直接申请和释放内存
   - **H264 Frame Queue**：使用V2队列管理，支持网络传输消费者

### 4.2 方案二：DVP摄像头 + 硬件双输出（YUV + H264）

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                  DVP Camera (硬件双输出：YUV + H264)                         │
│                      内部硬件模块直接输出两路数据                              │
└────────────────────┬───────────────────────────┬────────────────────────────┘
                     │                           │
                     │ YUV输出                   │ H264输出
                     ▼                           ▼
      ┌──────────────────────────┐   ┌──────────────────────────┐
      │  YUV Frame Queue (V2)    │   │  H264 Frame Queue (V2)   │
      │  - 多消费者支持           │   │  - 多消费者支持           │
      │  - consumer_mask: 0x02   │   │  - consumer_mask: 0x01   │
      └──────────┬───────────────┘   └──────────┬───────────────┘
                 │                              │
                 ▼                              ▼
      ┌─────────────────────┐         ┌─────────────────────┐
      │  LCD Display        │         │  WiFi Transfer      │
      │  Consumer (0x02)    │         │  Consumer (0x01)    │
      └──────────┬──────────┘         └────────┬────────────┘
                 │                              │
                 │ 视频处理：                    │
                 │ 1. 旋转(可选)                │
                 │ 2. 缩放(可选)                │
                 │ 3. 格式转换                  │
                 ▼                              ▼
      ┌─────────────────────┐         ┌─────────────────────┐
      │  LCD屏幕显示         │         │  网络传输           │
      │                     │         │  (CS2/TCP/UDP)      │
      └─────────────────────┘         └────────┬────────────┘
                                               │
                                               ▼
                                      ┌─────────────────────┐
                                      │  对端设备           │
                                      │  解码显示           │
                                      └─────────────────────┘
```

**流程说明：**

1. **DVP硬件双输出**
   - DVP摄像头内部集成硬件模块
   - 同时输出两路数据：

     * YUV原始数据 → 通过 `frame_queue_v2_complete()` 放入YUV Frame Queue
     * H264编码数据 → 通过 `frame_queue_v2_complete()` 放入H264 Frame Queue

   - **无需软件编码器**，硬件直接完成H264编码

2. **显示路径（YUV消费）**
   - LCD显示模块注册为YUV消费者（CONSUMER_DECODER = 0x02）
   - 通过 `frame_queue_v2_get_frame(IMAGE_YUV, CONSUMER_DECODER, timeout)` 获取YUV帧
   - 经过视频处理管道：

     * 软件/硬件旋转（支持0°/90°/180°/270°）
     * 图像缩放（适配LCD分辨率）
     * 像素格式转换（RGB565/RGB888等）

   - 显示到LCD屏幕
   - 使用完毕调用 `frame_queue_v2_release_frame(IMAGE_YUV, CONSUMER_DECODER, frame)` 释放

3. **网络传输路径（H264消费）**
   - WiFi传输模块注册为H264消费者（CONSUMER_TRANSMISSION = 0x01）
   - 通过 `frame_queue_v2_get_frame(IMAGE_H264, CONSUMER_TRANSMISSION, timeout)` 获取H264帧
   - 直接通过CS2/TCP/UDP协议传输到对端设备
   - 使用完毕调用 `frame_queue_v2_release_frame(IMAGE_H264, CONSUMER_TRANSMISSION, frame)` 释放

4. **性能优势**
   - **硬件双输出**：DVP内部一次处理，同时输出YUV和H264
   - **无软件编码开销**：H264由硬件直接生成，CPU占用最低
   - **零延迟共享**：两路数据独立，互不影响
   - **完全并行**：显示和传输完全独立，无竞争
   - **最低延迟**：端到端延迟最短，适合实时性要求最高的场景

5. **Frame Queue管理**
   - **YUV Frame Queue**：管理YUV帧，LCD显示消费
   - **H264 Frame Queue**：管理H264帧，网络传输消费
   - 两个队列完全独立，使用V2多消费者机制

### 4.3 两种方案对比

.. list-table:: 两种方案对比
   :header-rows: 1

   * - 对比项
     - 方案一：UVC摄像头
     - 方案二：DVP摄像头
   * - 摄像头输出
     - MJPEG压缩流
     - 硬件双输出：YUV + H264
   * - 使用的Frame Queue
     - MJPEG Queue + H264 Queue
     - YUV Queue + H264 Queue
   * - YUV数据管理
     - 解码后直接内存申请（不用队列）
     - DVP硬件输出到YUV Queue
   * - H264生成方式
     - 软件编码（CPU消耗）
     - 硬件直接输出（无CPU消耗）
   * - 是否需要解码
     - 需要MJPEG解码
     - 不需要解码
   * - 是否需要编码
     - 需要H264软件编码
     - 不需要编码（硬件输出）
   * - CPU占用
     - 高（解码 + 编码）
     - 低（仅视频处理）
   * - 端到端延迟
     - 较高（解码+编码）
     - 最低（硬件直出）
   * - 多消费者支持
     - MJPEG和H264支持
     - YUV和H264都支持
   * - 适用场景
     - USB摄像头
     - 板载DVP摄像头

### 4.4 关键技术特性

#### 4.4.1 多消费者架构
- **消费者类型**：
  - `CONSUMER_TRANSMISSION (0x01)`：网络传输
  - `CONSUMER_DECODER (0x02)`：解码器
  - `CONSUMER_STORAGE (0x04)`：存储（可选）
  - `CONSUMER_RECOGNITION (0x08)`：识别（可选）

#### 4.4.2 帧队列配置

.. list-table:: 帧队列配置
   :header-rows: 1

   * - 格式
     - 帧数量
     - 主要用途
     - 使用场景
   * - MJPEG
     - 4个
     - UVC摄像头输出，支持解码器消费
     - 方案一：UVC摄像头
   * - H264
     - 6个
     - 编码后的H264流，支持网络传输消费
     - 方案一：软件编码输出；方案二：DVP硬件输出
   * - YUV
     - 3个
     - DVP摄像头硬件输出，支持显示消费
     - 方案二：DVP摄像头

**重要说明**：

**方案一（UVC）**：
  - ✅ MJPEG Frame Queue：UVC摄像头输出
  - ❌ YUV数据：解码后**直接内存申请**（不使用YUV Frame Queue）
  - ✅ H264 Frame Queue：软件编码器输出

**方案二（DVP）**：
  - ✅ YUV Frame Queue：DVP硬件直接输出YUV
  - ✅ H264 Frame Queue：DVP硬件直接输出H264（**无需软件编码**）
  - 📌 关键优势：DVP内部硬件一次处理，同时输出两路数据

#### 4.4.3 性能优化
- **中断安全**：支持在中断上下文调用malloc/complete
- **零拷贝**：多消费者共享同一帧数据
- **自动复用**：frame buffer自动回收复用
- **慢消费者保护**：自动丢弃旧帧，不阻塞系统

#### 4.4.4 典型性能指标

**方案一（UVC）性能**：
  - **MJPEG解码**：864x480@30fps，硬解延迟<33ms
  - **H264软件编码**：864x480@30fps，延迟<50ms
  - **端到端延迟**：<200ms（WiFi正常条件下）
  - **CPU占用**：中等（解码 + 编码）

**方案二（DVP）性能**：
  - **YUV输出**：硬件直出，零延迟
  - **H264输出**：硬件直出，零延迟
  - **端到端延迟**：<100ms（WiFi正常条件下，最优）
  - **CPU占用**：极低（仅YUV处理）

**网络传输**：
  - 支持1Mbps~8Mbps自适应码率
  - 支持CS2/TCP/UDP多种协议


## 5 API参考

本章节提供项目中核心功能的API接口说明，这些接口通过封装SDK实现了高级功能调用。

.. note::

   建议开发者不要直接调用以下接口实现自定义方案，而是参考这些接口的实现方式，通过组合封装SDK接口来构建符合自身需求的功能模块。

### 5.1 摄像头管理API

#### 5.1.1 doorbell_camera_turn_on
```c
/**
 * @brief 开启摄像头设备
 * 
 * @param parameters 摄像头参数结构体指针
 *        - id: 摄像头设备ID (UVC_DEVICE_ID或其他DVP设备ID)
 *        - width: 图像宽度
 *        - height: 图像高度
 *        - format: 图像格式 (0:MJPEG, 1:H264)
 *        - protocol: 传输协议
 *        - rotate: 旋转角度
 * 
 * @return int 操作结果
 *         - BK_OK: 成功
 *         - BK_FAIL: 失败
 * 
 * @note 此函数会：
 *       1. 初始化frame queue V2用于图像帧缓存管理（多消费者支持）
 *          - Frame_buffer: frame_queue_v2_init_all
 *       2. 根据摄像头类型(UVC或DVP)分别调用相应的开启函数
 *          - DVP: dvp_camera_turn_on 
 *            * 硬件直接输出YUV到YUV Frame Queue
 *            * 硬件直接输出H264到H264 Frame Queue
 *          - UVC: uvc_camera_turn_on 
 *            * 输出MJPEG到MJPEG Frame Queue
 *       3. 初始化视频处理管道和H264编码器（仅UVC方案需要）
 *          - H264: doorbell_h264_encode_turn_on（DVP方案无需此步骤）
 *       4. 配置图像旋转处理（如果显示控制器已初始化）
 *          - ROTATE: bk_video_pipeline_open_rotate
 *       5. 自动检测并选择硬件或软件解码（仅UVC方案）
 * 
 * @note 内存管理方式：
 *       - UVC方案：MJPEG使用队列，解码后YUV直接申请内存，H264软件编码后使用队列
 *       - DVP方案：硬件直接输出YUV和H264，都使用Frame Queue管理（无需软件编码）
 */
int doorbell_camera_turn_on(camera_parameters_t *parameters);
```

#### 5.1.2 doorbell_camera_turn_off
```c
/**
 * @brief 关闭摄像头设备
 * 
 * @return int 操作结果
 *         - BK_OK: 成功
 *         - BK_FAIL: 失败
 * 
 * @note 此函数会：
 *       1. 如果当前摄像头类型为UVC摄像头，关闭H264编码器管道（软件编码器）
 *       2. 根据摄像头类型调用相应的关闭函数
 *          - UVC: uvc_camera_turn_off()
 *            * 关闭H264软件编码器（如果已开启）
 *            * 断开UVC设备连接
 *            * 释放MJPEG解码器资源
 *          - DVP: dvp_camera_turn_off()
 *            * 停止DVP硬件输出（YUV + H264）
 *            * 关闭摄像头电源
 *       3. 释放摄像头相关资源，包括：
 *          - 关闭摄像头硬件
 *          - 删除摄像头控制器
 *          - 取消flash操作通知注册
 * 
 * @warning 调用此函数前应确保摄像头已正确开启，否则可能导致资源泄漏
 * @see doorbell_camera_turn_on()
 */
int doorbell_camera_turn_off(void);
```

### 5.2 H.264编码API（仅UVC方案使用）

> **注意**：此API仅用于UVC方案的软件H264编码。DVP方案使用硬件直接输出H264，无需此编码器。

#### 5.2.1 doorbell_h264_encode_turn_on
```c
/**
 * @brief 开启H264编码器（仅UVC方案）
 * 
 * @param parameters 摄像头参数结构体指针
 *        - width: 编码图像宽度
 *        - height: 编码图像高度
 *        - rotate: 图像旋转角度
 * 
 * @return int 操作结果
 *         - BK_OK: 成功
 *         - BK_FAIL: 失败
 * 
 * @note 此函数仅在UVC方案中使用，用于将解码后的YUV编码为H264：
 *       1. 配置视频处理管道参数，包括JPEG解码回调函数
 *       2. 如果视频管道句柄为空，创建新的视频处理管道
 *       3. 配置H264编码器参数：
 *          - 设置编码分辨率（width x height）
 *          - 设置帧率为30FPS
 *          - 配置软件旋转角度
 *          - 设置H264编码回调函数（内存分配和编码完成回调）
 *       4. 打开H264编码器管道
 *       5. 使用doorbell_h264e_cbs回调结构体：
 *          - h264e_frame_malloc: 从H264 Frame Queue分配内存
 *          - h264e_frame_complete: 编码完成后放入H264 Frame Queue
 * 
 * @note DVP方案不调用此函数，因为DVP硬件直接输出H264到H264 Frame Queue
 * 
 * @warning 调用此函数前应确保摄像头设备已正确初始化
 * @see doorbell_h264_encode_turn_off()
 */
int doorbell_h264_encode_turn_on(camera_parameters_t *parameters);
```

#### 5.2.2 doorbell_h264_encode_turn_off
```c
/**
 * @brief 关闭H264编码器（仅UVC方案）
 * 
 * @return int 操作结果
 *         - BK_OK: 成功
 *         - BK_FAIL: 失败
 * 
 * @note 此函数仅在UVC方案中使用：
 *       1. 检查视频管道句柄是否为空，如果为空则直接返回成功
 *       2. 调用bk_video_pipeline_close_h264e()关闭H264编码器管道
 *       3. 记录关闭日志信息
 *       4. 释放H264编码器相关资源
 * 
 * @note DVP方案不调用此函数，因为使用硬件直接输出H264
 * 
 * @attention 此函数仅关闭H264编码器，不会关闭整个摄像头设备
 * @warning 调用此函数前应确保H264编码器已正确开启
 * @see doorbell_h264_encode_turn_on()
 */
int doorbell_h264_encode_turn_off(void);
```

### 5.3 图传API

#### 5.3.1 doorbell_video_transfer_turn_on
```c
/**
 * @brief 开启视频传输功能
 * 
 * @return int 操作结果
 *         - BK_OK: 操作成功
 *         - BK_FAIL: 操作失败
 * 
 * @note 此函数负责开启视频传输功能，主要功能包括：
 *        - 检查视频信息结构体是否有效
 *        - 检查摄像头是否已开启
 *        - 验证视频传输回调函数是否设置
 *        - 注册传输任务为H264帧队列消费者（CONSUMER_TRANSMISSION）
 *        - 通过WiFi传输框架开启视频帧传输
 *        - 根据传输格式配置传输参数
 * 
 * @note 数据来源：
 *        - UVC方案：从H264 Frame Queue获取软件编码后的H264帧
 *        - DVP方案：从H264 Frame Queue获取硬件直接输出的H264帧
 * 
 * @warning 调用此函数前应确保摄像头已正确开启
 * 
 * @see doorbell_video_transfer_turn_off()
 */
int doorbell_video_transfer_turn_on(void);
```

#### 5.3.2 doorbell_video_transfer_turn_off
```c
/**
 * @brief 关闭视频传输功能
 * 
 * @return int 操作结果
 *         - BK_OK: 操作成功
 *         - BK_FAIL: 操作失败
 * 
 * @note 此函数负责关闭视频传输功能，主要功能包括：
 *        - 检查视频信息结构体是否有效
 *        - 检查摄像头是否已开启
 *        - 通过WiFi传输框架关闭视频帧传输
 *        - 注销传输任务消费者（CONSUMER_TRANSMISSION）
 *        - 自动回收该消费者未访问的帧
 *        - 清理视频传输相关资源
 *        - 根据配置关闭CS2图像定时器
 * 
 * @warning 调用此函数前应确保视频传输功能已正确开启
 * 
 * @see doorbell_video_transfer_turn_on()
 */
int doorbell_video_transfer_turn_off(void);
```

### 5.4 显示API

#### 5.4.1 doorbell_display_turn_on
```c
/**
 * @brief 开启显示设备
 * 
 * @param parameters 显示参数结构体指针
 *        - id: 显示设备ID
 *        - rotate_angle: 旋转角度
 *        - pixel_format: 像素格式 (0:硬件旋转, 1:软件旋转)
 * 
 * @return int 操作结果
 *         - EVT_STATUS_OK: 成功
 *         - EVT_STATUS_ERROR: 失败
 *         - EVT_STATUS_ALREADY: 设备已开启
 * 
 * @note 此函数会：
 *       1. 初始化frame queue V2用于图像帧缓存管理（多消费者支持）
 *       2. 检查显示设备是否已开启，如果已开启则返回EVT_STATUS_ALREADY
 *       3. 根据设备ID获取LCD设备配置
 *       4. 根据LCD类型(RGB/MCU8080)创建相应的显示控制器
 *       5. 创建视频处理管道并配置旋转参数
 *       6. 如果使用DVP摄像头，注册YUV帧队列的显示消费者
 *       7. 打开显示控制器和LCD背光
 *       8. 设置设备信息结构体中的LCD相关参数
 * 
 * @note 数据来源和内存管理：
 *       - DVP方案：
 *         * 从YUV Frame Queue获取DVP硬件输出的YUV帧
 *         * 注册为YUV消费者（CONSUMER_DECODER）
 *         * 显示后通过frame_queue_v2_release_frame释放
 *       - UVC方案：
 *         * 解码器直接申请YUV内存（不使用YUV Frame Queue）
 *         * 视频处理管道直接使用解码器提供的YUV数据
 *         * 显示后直接释放内存（不调用frame_queue_v2_release_frame）
 * 
 * @warning 如果设备初始化失败，会清理已分配的资源
 * @see doorbell_display_turn_off()
 */
int doorbell_display_turn_on(display_parameters_t *parameters);
```

#### 5.4.2 doorbell_display_turn_off
```c
/**
 * @brief 关闭显示设备
 * 
 * @return int 操作结果
 *         - 0: 成功
 *         - EVT_STATUS_ALREADY: 设备已关闭
 *         - EVT_STATUS_ERROR: 失败
 * 
 * @note 此函数会：
 *       1. 检查显示设备是否已关闭，如果已关闭则返回EVT_STATUS_ALREADY
 *       2. 关闭LCD背光
 *       3. 关闭视频处理管道的旋转功能
 *       4. 如果使用DVP摄像头，注销YUV帧队列的显示消费者（CONSUMER_DECODER）
 *       5. 自动回收该消费者未访问的YUV帧（仅DVP方案）
 *       6. 关闭显示控制器
 *       7. 删除显示控制器句柄
 *       8. 重置设备信息结构体中的LCD相关参数
 * 
 * @note 消费者注销：
 *       - DVP方案：调用frame_queue_v2_unregister_consumer注销YUV消费者
 *       - UVC方案：不需要注销消费者（解码的YUV数据不使用帧队列）
 * 
 * @warning 此函数会释放所有显示相关的资源
 * @see doorbell_display_turn_on()
 */
int doorbell_display_turn_off(void);
```

### 5.5 音频API

#### 5.5.1 doorbell_audio_turn_on
```c
/**
 * @brief 开启音频设备
 * 
 * @param parameters 音频参数结构体指针
 *        - aec: 回声消除使能标志
 *        - uac: USB音频设备使能标志
 *        - rmt_recorder_sample_rate: 远程录音采样率
 *        - rmt_player_sample_rate: 远程播放采样率
 *        - rmt_recoder_fmt: 远程录音编码格式
 *        - rmt_player_fmt: 远程播放编码格式
 * 
 * @return int 操作结果
 *         - BK_OK: 操作成功
 *         - BK_FAIL: 操作失败或设备已开启
 * 
 * @note 此函数负责开启音频设备，主要功能包括：
 *        - 检查音频设备是否已开启
 *        - 配置音频参数（采样率、编码格式等）
 *        - 根据UAC标志选择音频配置方式
 *        - 配置AEC回声消除参数
 *        - 初始化音频编码器/解码器
 *        - 初始化音频读取和写入句柄
 *        - 启动音频处理流程
 * 
 * @warning 调用此函数前应确保音频参数正确配置
 * 
 * @see doorbell_audio_turn_off()
 */
int doorbell_audio_turn_on(audio_parameters_t *parameters);
```

#### 5.5.2 doorbell_audio_turn_off
```c
/**
 * @brief 关闭音频设备
 * 
 * @return int 操作结果
 *         - BK_OK: 操作成功
 *         - BK_FAIL: 操作失败或设备已关闭
 * 
 * @note 此函数负责关闭音频设备，主要功能包括：
 *        - 检查音频设备是否已关闭
 *        - 设置音频设备状态为关闭
 *        - 通知服务音频状态变化
 *        - 停止音频读取和写入操作
 *        - 反初始化音频相关句柄
 *        - 清理音频资源
 * 
 * @warning 调用此函数前应确保音频设备已正确初始化
 * 
 * @see doorbell_audio_turn_on()
 */
int doorbell_audio_turn_off(void);
```

### 5.6 帧缓冲区队列管理API（V2多消费者版本）

本项目使用帧队列V2版本，支持多消费者模式、引用计数和自动内存管理。主要特性：
- **多消费者支持**：多个消费者可同时访问同一帧
- **引用计数管理**：自动跟踪帧的使用状态
- **慢消费者保护**：自动丢弃旧帧，不阻塞快速消费者
- **动态注册**：支持消费者动态注册和注销
- **内存复用**：frame buffer自动复用，减少内存分配

#### 消费者类型定义
```c
#define CONSUMER_TRANSMISSION   (1 << 0)  // 视频传输任务
#define CONSUMER_DECODER        (1 << 1)  // 解码器任务
#define CONSUMER_STORAGE        (1 << 2)  // 存储任务
#define CONSUMER_RECOGNITION    (1 << 3)  // 识别任务
#define CONSUMER_CUSTOM_1       (1 << 4)  // 自定义任务1
#define CONSUMER_CUSTOM_2       (1 << 5)  // 自定义任务2
#define CONSUMER_CUSTOM_3       (1 << 6)  // 自定义任务3
#define CONSUMER_CUSTOM_4       (1 << 7)  // 自定义任务4
```

#### 5.6.1 frame_queue_v2_init_all
```c
/**
 * @brief 初始化所有图像格式的帧队列数据结构（V2版本）
 * 
 * @return bk_err_t 初始化结果
 *         - BK_OK: 所有队列初始化成功
 *         - BK_FAIL: 任一队列初始化失败
 * 
 * @note 此函数初始化V2版本的帧队列系统，支持：
 *        - MJPEG格式帧队列（默认4个帧缓存）
 *        - H264格式帧队列（默认6个帧缓存）
 *        - YUV格式帧队列（默认3个帧缓存）
 *        - 双链表管理结构
 *        - 多消费者支持（最多8个）
 *        - 线程安全的临界区保护
 * 
 * @warning 调用此函数前应确保系统资源充足
 * 
 * @see frame_queue_v2_deinit_all()
 */
bk_err_t frame_queue_v2_init_all(void);
```

#### 5.6.2 frame_queue_v2_deinit_all
```c
/**
 * @brief 释放所有图像格式的帧队列数据结构（V2版本）
 * 
 * @return bk_err_t 释放结果
 *         - BK_OK: 所有队列释放成功
 * 
 * @note 此函数负责释放V2帧队列的所有资源：
 *        - 释放free_list中的所有节点
 *        - 释放ready_list中的所有节点
 *        - 释放所有frame buffer内存
 *        - 反初始化信号量
 *        - 打印统计信息（malloc/complete/free计数）
 * 
 * @see frame_queue_v2_init_all()
 */
bk_err_t frame_queue_v2_deinit_all(void);
```

#### 5.6.3 frame_queue_v2_register_consumer
```c
/**
 * @brief 注册消费者
 * 
 * @param format 图像格式
 *        - IMAGE_MJPEG: MJPEG格式
 *        - IMAGE_H264: H264格式
 *        - IMAGE_YUV: YUV格式
 * 
 * @param consumer_id 消费者ID（CONSUMER_XXX宏定义）
 *        - 每个消费者使用唯一的位标识
 * 
 * @return bk_err_t 操作结果
 *         - BK_OK: 注册成功
 *         - BK_FAIL: 注册失败
 * 
 * @note 此函数注册一个新消费者：
 *        - 更新active_consumers掩码
 *        - 初始化消费者信息结构
 *        - 清理ready_list中consumer_mask=0的旧帧
 *        - 之后该消费者可以访问所有新产生的帧
 * 
 * @warning 消费者ID必须是2的幂（单一bit位）
 * 
 * @see frame_queue_v2_unregister_consumer()
 * @see frame_queue_v2_get_frame()
 */
bk_err_t frame_queue_v2_register_consumer(image_format_t format, uint32_t consumer_id);
```

#### 5.6.4 frame_queue_v2_unregister_consumer
```c
/**
 * @brief 注销消费者
 * 
 * @param format 图像格式
 * @param consumer_id 消费者ID
 * 
 * @return bk_err_t 操作结果
 *         - BK_OK: 注销成功
 *         - BK_FAIL: 注销失败
 * 
 * @note 此函数注销消费者并清理资源：
 *        - 从active_consumers中移除该消费者
 *        - 更新ready_list中所有未访问帧的consumer_mask
 *        - 自动回收不再需要的帧到free_list（保留frame buffer供复用）
 *        - 打印统计信息（get/release计数）
 * 
 * @warning 注销后该消费者不应再调用get_frame/release_frame
 * 
 * @see frame_queue_v2_register_consumer()
 */
bk_err_t frame_queue_v2_unregister_consumer(image_format_t format, uint32_t consumer_id);
```

#### 5.6.5 frame_queue_v2_malloc
```c
/**
 * @brief 分配帧缓存（生产者使用）
 * 
 * @param format 图像格式
 * @param size 请求的帧大小
 * 
 * @return frame_buffer_t* 分配的帧缓存指针，失败返回NULL
 * 
 * @note 此函数为生产者分配帧缓存：
 *        - 从free_list获取空闲节点
 *        - 复用现有frame buffer或分配新的
 *        - 设置consumer_mask为当前active_consumers
 *        - 支持中断上下文调用（使用spinlock保护）
 *        - 如果free_list为空，对于MJPEG和YUV会自动复用最旧的帧
 * 
 * @warning 分配后必须调用complete或cancel
 * 
 * @see frame_queue_v2_complete()
 * @see frame_queue_v2_cancel()
 */
frame_buffer_t *frame_queue_v2_malloc(image_format_t format, uint32_t size);
```

#### 5.6.6 frame_queue_v2_complete
```c
/**
 * @brief 将填充好的帧放入就绪队列（生产者使用）
 * 
 * @param format 图像格式
 * @param frame 填充好的帧缓存
 * 
 * @return bk_err_t 操作结果
 *         - BK_OK: 放入成功
 *         - BK_FAIL: 放入失败
 * 
 * @note 此函数将帧提交给消费者：
 *        - 从free_list移到ready_list
 *        - 设置帧的timestamp和consumer_mask
 *        - 通过信号量唤醒等待的消费者
 *        - 支持中断上下文调用
 * 
 * @warning 不要对同一帧重复调用complete
 * 
 * @see frame_queue_v2_malloc()
 * @see frame_queue_v2_get_frame()
 */
bk_err_t frame_queue_v2_complete(image_format_t format, frame_buffer_t *frame);
```

#### 5.6.7 frame_queue_v2_cancel
```c
/**
 * @brief 取消已分配但失败的帧（生产者使用）
 * 
 * @param format 图像格式
 * @param frame 要取消的帧缓存
 * 
 * @return bk_err_t 操作结果
 *         - BK_OK: 取消成功
 *         - BK_FAIL: 取消失败
 * 
 * @note 当生产者在malloc后遇到错误时调用：
 *        - 重置节点的in_use标志
 *        - 保留frame buffer供下次复用
 *        - 节点保留在free_list中
 *        - 避免内存泄漏
 * 
 * @see frame_queue_v2_malloc()
 */
bk_err_t frame_queue_v2_cancel(image_format_t format, frame_buffer_t *frame);
```

#### 5.6.8 frame_queue_v2_get_frame
```c
/**
 * @brief 消费者获取帧（支持多消费者同时访问同一帧）
 * 
 * @param format 图像格式
 * @param consumer_id 消费者ID
 * @param timeout 超时时间（毫秒）
 *        - 0: 非阻塞，立即返回
 *        - >0: 等待指定毫秒数
 *        - BEKEN_WAIT_FOREVER: 永久等待
 * 
 * @return frame_buffer_t* 获取的帧缓存指针，失败返回NULL
 * 
 * @note 多消费者安全访问：
 *        - 自动跳过已访问的帧
 *        - 自动清理consumer_mask=0的过期帧
 *        - 增加帧的引用计数
 *        - 标记该消费者已访问
 *        - 使用后必须调用release_frame
 * 
 * @warning 必须先调用register_consumer注册
 * 
 * @see frame_queue_v2_register_consumer()
 * @see frame_queue_v2_release_frame()
 */
frame_buffer_t *frame_queue_v2_get_frame(image_format_t format, uint32_t consumer_id, uint32_t timeout);
```

#### 5.6.9 frame_queue_v2_release_frame
```c
/**
 * @brief 消费者释放帧
 * 
 * @param format 图像格式
 * @param consumer_id 消费者ID
 * @param frame 要释放的帧缓存
 * 
 * @return void
 * 
 * @note 引用计数管理：
 *        - 减少帧的引用计数
 *        - 当ref_count=0且所有需要访问的消费者都已访问时：
 *          * 释放frame buffer内存
 *          * 重置节点状态
 *          * 将节点移回free_list
 *        - 线程安全，支持多个消费者并发释放
 * 
 * @warning 不要重复释放同一帧
 * 
 * @see frame_queue_v2_get_frame()
 */
void frame_queue_v2_release_frame(image_format_t format, uint32_t consumer_id, frame_buffer_t *frame);
```

#### 5.6.10 frame_queue_v2_get_stats
```c
/**
 * @brief 获取队列统计信息
 * 
 * @param format 图像格式
 * @param free_count 输出：空闲帧数量
 * @param ready_count 输出：就绪帧数量
 * @param total_malloc 输出：总分配次数
 * @param total_complete 输出：总完成次数
 * @param total_free 输出：总释放次数
 * 
 * @return bk_err_t 操作结果
 *         - BK_OK: 获取成功
 *         - BK_FAIL: 获取失败
 * 
 * @note 用于性能监控和调试
 */
bk_err_t frame_queue_v2_get_stats(image_format_t format, 
                                   uint32_t *free_count, 
                                   uint32_t *ready_count,
                                   uint32_t *total_malloc,
                                   uint32_t *total_complete,
                                   uint32_t *total_free);
```

### 5.7 帧队列V2使用示例

#### 5.7.1 生产者示例（摄像头采集）
```c
// 1. 初始化帧队列
frame_queue_v2_init_all();

// 2. 分配帧缓存
frame_buffer_t *frame = frame_queue_v2_malloc(IMAGE_MJPEG, 100*1024);
if (frame == NULL) {
    // 分配失败处理
    return;
}

// 3. 填充数据到frame->frame
// ... camera capture data ...

// 4. 填充成功，放入就绪队列
frame_queue_v2_complete(IMAGE_MJPEG, frame);

// 或者填充失败，取消该帧
// frame_queue_v2_cancel(IMAGE_MJPEG, frame);
```

#### 5.7.2 消费者示例（视频传输）
```c
// 1. 注册为传输消费者
frame_queue_v2_register_consumer(IMAGE_MJPEG, CONSUMER_TRANSMISSION);

// 2. 获取帧（阻塞等待）
frame_buffer_t *frame = frame_queue_v2_get_frame(IMAGE_MJPEG, 
                                                  CONSUMER_TRANSMISSION, 
                                                  BEKEN_WAIT_FOREVER);
if (frame) {
    // 3. 使用帧数据
    // ... send frame data ...

    // 4. 使用完毕，释放帧
    frame_queue_v2_release_frame(IMAGE_MJPEG, CONSUMER_TRANSMISSION, frame);
}

// 5. 关闭时注销消费者
frame_queue_v2_unregister_consumer(IMAGE_MJPEG, CONSUMER_TRANSMISSION);
```

#### 5.7.3 多消费者示例
```c
// 场景：同时支持图传和存储

// 消费者1：视频传输任务
frame_queue_v2_register_consumer(IMAGE_H264, CONSUMER_TRANSMISSION);

// 消费者2：存储任务
frame_queue_v2_register_consumer(IMAGE_H264, CONSUMER_STORAGE);

// 两个消费者可以同时访问同一帧
// 消费者1获取并使用
frame_buffer_t *frame1 = frame_queue_v2_get_frame(IMAGE_H264, CONSUMER_TRANSMISSION, 100);
// 传输处理...
frame_queue_v2_release_frame(IMAGE_H264, CONSUMER_TRANSMISSION, frame1);

// 消费者2获取并使用（同一帧）
frame_buffer_t *frame2 = frame_queue_v2_get_frame(IMAGE_H264, CONSUMER_STORAGE, 100);
// 存储处理...
frame_queue_v2_release_frame(IMAGE_H264, CONSUMER_STORAGE, frame2);

// 只有当两个消费者都释放后，帧才会真正回收
```

#### 5.7.4 慢消费者处理
```c
// V2版本自动处理慢消费者：
// - 快速消费者不会被慢消费者阻塞
// - 慢消费者会自动丢失一些旧帧
// - 慢消费者总是能获取到最新的可用帧

// 示例：传输任务处理较慢
frame_queue_v2_register_consumer(IMAGE_MJPEG, CONSUMER_TRANSMISSION);

while (running) {
    // 即使处理较慢，也会跳过旧帧，获取最新帧
    frame_buffer_t *frame = frame_queue_v2_get_frame(IMAGE_MJPEG, 
                                                      CONSUMER_TRANSMISSION, 
                                                      100);
    if (frame) {
        // 慢速处理...
        slow_network_send(frame);
        frame_queue_v2_release_frame(IMAGE_MJPEG, CONSUMER_TRANSMISSION, frame);
    }
}
```

## 6 注意事项

1. 默认使用GPIO_28控制USB的LDO，拉高上电，注意GPIO冲突问题
2. 默认使用GPIO_13控制LCD的LDO，拉高上电，注意GPIO冲突问题
3. 默认使用GPIO_7控制LCD的背光，拉高有效，注意GPIO冲突问题

4. **帧队列V2注意事项**：
   - 消费者必须先注册才能获取帧
   - 获取的帧使用后必须调用release_frame释放
   - 不要重复释放同一帧
   - 消费者关闭前必须注销，避免资源泄漏
   - V2版本会自动复用frame buffer，无需手动管理内存

5. **内存管理差异**：
   - UVC方案（MJPEG软解码 + 软件H264编码）：

     * ✅ MJPEG帧：使用MJPEG Frame Queue V2管理
     * ❌ 解码后的YUV数据：**直接内存申请**，不使用YUV Frame Queue
     * ❌ 视频处理管道的输出：直接内存申请
     * ✅ H264帧：软件编码后使用H264 Frame Queue管理
     * 📌 CPU开销：需要软件解码MJPEG + 软件编码H264

   - DVP方案（硬件双输出）：

     * ✅ YUV帧：DVP硬件直接输出到YUV Frame Queue V2
     * ✅ H264帧：DVP硬件直接输出到H264 Frame Queue V2
     * ❌ 无需软件编码器：DVP内部ISP硬件同时生成两路数据
     * 📌 CPU开销：最低，硬件直接输出

   - 关键区别：

     * YUV Frame Queue**仅用于DVP摄像头**硬件输出
     * UVC方案的YUV数据**不经过队列**，直接内存申请
     * DVP方案**无需H264编码器**，硬件直接输出H264

## 7 系统架构

项目采用模块化设计，主要包含以下模块：

1. **WiFi模块**：负责网络连接和数据传输
2. **媒体处理模块**：处理图像采集、编码和存储
3. **显示模块**：管理LCD显示
4. **蓝牙模块**：提供蓝牙通信功能

各模块之间通过明确的API接口进行交互，保证了系统的可维护性和扩展性。

## 8 配置说明

项目的主要配置选项位于Kconfig文件中，可以通过修改配置来启用或禁用特定功能：

## 9 故障排除

### 9.1 常见问题及解决方案

#### 9.1.1 摄像头无法识别

   - 检查UVC摄像头连接是否正确，USB接口是否松动
   - 确保摄像头电源供应正常，检查USB LDO是否拉高
   - 确认摄像头驱动是否正确加载，可通过日志查看初始化过程
   - 尝试更换兼容的UVC摄像头模块

#### 9.1.2 显示异常

   - 检查LCD连接是否正确，排线是否插紧
   - 确认LCD LDO和背光是否正常工作
   - 检查LCD型号是否与配置匹配

#### 9.1.3 WiFi连接失败

   - 确认WiFi名称和密码输入正确
   - 确保使用的是2.4G WiFi网络（不支持5G WiFi）
   - 检查设备与路由器的距离是否过远
   - 尝试通过蓝牙配网功能重新连接

#### 9.1.4 视频传输卡顿

   - 检查网络连接质量，确保带宽充足
   - 尝试降低视频分辨率和帧率设置
   - 确认H.264编码器工作正常
   - 检查帧缓冲区管理是否存在异常
   - 使用 `frame_queue_v2_get_stats()` 查看队列统计信息
