# Doorviewer项目开发指南

* [English](./README.md)

## 1 项目概述

本项目是一个基于BK7258芯片的智能门铃门锁解决方案，实现了通过WiFi传输MJPEG图像数据并在LCD屏幕上显示的功能。项目集成了丰富的多媒体处理能力、网络通信功能和用户界面显示，适用于智能门铃门锁设备的开发。

**关键区别**：
- **doorviewer项目**：图传使用MJPEG格式，带宽占用较大，兼容性更好
- **doorbell项目**：图传使用H264格式，带宽占用小，压缩效率更高

## 2 功能特性

### 2.1 WiFi通信
- 支持STA模式连接到现有WiFi网络
- 支持AP模式创建WiFi热点供BK7258设备连接
- 支持TCP/UDP协议传输图像数据
- 支持CS2实时网络传输
- **主要特性**：图传采用MJPEG格式，无需H264编码，兼容性更好

### 2.2 多媒体处理
- 支持UVC摄像头控制与图像采集，默认格式为MJPEG，分辨率为864x480，帧率为30fps
- 支持DVP摄像头控制与图像采集，默认格式为MJPEG，分辨率为864x480，帧率为15fps
- 支持DVP摄像头双输出模式（MJPEG + YUV同时输出）
- 支持软件或硬件MJPEG解码，系统内部自动选择
- 支持MJPEG/YUV/H264帧缓冲区管理
- 支持多种LCD屏幕显示，RGB屏或MCU屏，默认使用RGB屏（st7701sn）
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

编译流程参考 `Doorviewer 解决方案 <../../index.html>`_

烧录流程参考 `烧录代码 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.0.1/get-started/index.html#id7>`_

编译生成的烧录bin文件路径：``projects/doorviewer/build/bk7258/doorviewer/package/all-app.bin``

### 3.3 基本操作流程
1. 设备上电启动
2. 测试机（Android）下载IOT应用到设备，下载地址： <https://dl.bekencorp.com/apk/BekenIot.apk>
3. 自行创建账号，并完成登录
4. 测试机打开IOT应用，添加设备，选择： `可视门铃`
5. `开始添加`，选择非5G的WiFi，连接成功后，点击下一步，开始通过蓝牙进行配网
6. 检查扫描到的设备蓝牙广播，点击IP地址匹配的进行连接，会自动完成100%的配网
7. 配网完成之后会自动打开摄像头，且打开网络图传，传输的格式是MJPEG，图像的分辨率为864x480
8. 打开其他外设，可以在IOT应用上进行控制

## 4 doorviewer视频流方案

本项目支持两种摄像头方案，核心区别在于**图传使用MJPEG格式**，无需H264编码，兼容性更好但带宽占用较大。

### 4.1 方案一：UVC/DVP摄像头 + MJPEG输出 + MJPEG网络传输

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    UVC/DVP Camera (MJPEG输出)                                │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                                       ▼
                    ┌──────────────────────────────┐
                    │  MJPEG Frame Queue           │
                    │  - frame_queue_malloc        │
                    │  - 用于缓存MJPEG压缩数据      │
                    └──────────────┬───────────────┘
                                   │
                    ┌──────────────┴──────────────┐
                    │                             │
                    ▼                             ▼
         ┌─────────────────────┐      ┌────────────────────┐
         │  WiFi Transfer      │      │  MJPEG Decoder     │
         │  (直接传输MJPEG)     │      │  (本地显示)        │
         │                     │      │                    │
         │  Path 1: 网络传输    │      │  Path 2: LCD显示   │
         └──────────┬──────────┘      └────────┬───────────┘
                    │                           │
                    │ MJPEG流                   │ 解码为YUV
                    ▼                           ▼
         ┌─────────────────────┐      ┌────────────────────┐
         │  网络传输            │      │  YUV Buffer        │
         │  (CS2/TCP/UDP)      │      │  (直接内存申请)    │
         └──────────┬──────────┘      └────────┬───────────┘
                    │                          │
                    ▼                          ▼
         ┌─────────────────────┐      ┌────────────────────┐
         │  对端设备            │      │  Video Pipeline    │
         │  MJPEG解码显示       │      │  视频处理：        │
         └─────────────────────┘      │  1. 旋转(可选)     │
                                      │  2. 缩放(可选)     │
                                      │  3. 格式转换       │
                                      └────────┬───────────┘
                                               │
                                               ▼
                                      ┌────────────────────┐
                                      │  LCD Display       │
                                      │  Driver            │
                                      └────────────────────┘
```

**流程说明：**

1. **MJPEG采集**
   - UVC/DVP摄像头直接输出MJPEG压缩流（默认864x480@30fps）
   - MJPEG帧通过 `frame_queue_complete()` 放入MJPEG帧队列
   - 支持两路消费：网络传输和本地解码显示

2. **网络传输路径（Path 1）**
   - **关键特性**：直接传输MJPEG流，无需H264编码
   - WiFi传输模块从MJPEG帧队列获取数据
   - 通过 `frame_queue_get_frame()` 获取MJPEG帧
   - 通过CS2/TCP/UDP协议直接传输MJPEG数据
   - 对端设备接收后进行MJPEG解码显示
   - **优势**：兼容性好，无需H264编解码器
   - **劣势**：带宽占用较大（约2-4Mbps）

3. **本地显示路径（Path 2）**
   - MJPEG解码器从帧队列获取MJPEG帧
   - 解码为YUV格式（直接内存申请，不使用YUV Frame Queue）
   - YUV数据通过视频处理管道：

     * 软件/硬件旋转（支持0°/90°/180°/270°）
     * 图像缩放（适配LCD分辨率）
     * 像素格式转换（RGB565/RGB888等）

   - 处理后的图像通过LCD驱动显示到屏幕

4. **内存管理**
   - **MJPEG Frame Queue**：使用frame_queue管理，支持多消费者访问
   - **YUV Buffer**：解码模块直接申请和释放内存，不使用队列
   - **处理后图像**：视频处理管道直接申请和释放内存

### 4.2 方案二：DVP摄像头双输出（MJPEG + YUV）+ MJPEG网络传输

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                  DVP Camera (双输出：MJPEG + YUV)                            │
│                      内部硬件模块直接输出两路数据                              │
└────────────────────┬───────────────────────────┬────────────────────────────┘
                     │                           │
                     │ MJPEG输出                 │ YUV输出
                     ▼                           ▼
      ┌──────────────────────────┐   ┌──────────────────────────┐
      │  MJPEG Frame Queue       │   │  YUV Frame Queue         │
      │  - frame_queue_malloc    │   │  - frame_queue_malloc    │
      └──────────┬───────────────┘   └──────────┬───────────────┘
                 │                              │
                 ▼                              ▼
      ┌─────────────────────┐         ┌─────────────────────┐
      │  WiFi Transfer       │         │  LCD Display        │
      │  (直接传输MJPEG)     │         │  (直接使用YUV)      │
      └──────────┬──────────┘         └────────┬────────────┘
                 │                              │
                 │ MJPEG流                      │ 视频处理：
                 │                              │ 1. 旋转(可选)
                 │                              │ 2. 缩放(可选)
                 │                              │ 3. 格式转换
                 ▼                              ▼
      ┌─────────────────────┐         ┌─────────────────────┐
      │  网络传输            │         │  LCD屏幕显示         │
      │  (CS2/TCP/UDP)      │         │                     │
      └────────┬────────────┘         └─────────────────────┘
               │
               ▼
      ┌─────────────────────┐
      │  对端设备            │
      │  MJPEG解码显示       │
      └─────────────────────┘
```

**流程说明：**

1. **DVP硬件双输出**
   - DVP摄像头内部集成硬件模块
   - 同时输出两路数据：

     * MJPEG压缩数据 → 通过 `frame_queue_complete()` 放入MJPEG Frame Queue
     * YUV原始数据 → 通过 `frame_queue_complete()` 放入YUV Frame Queue

   - **无需软件解码器**：LCD直接使用YUV数据

2. **网络传输路径（MJPEG消费）**
   - WiFi传输模块从MJPEG队列获取数据
   - 通过 `frame_queue_get_frame(IMAGE_MJPEG, timeout)` 获取MJPEG帧
   - 直接通过CS2/TCP/UDP协议传输MJPEG数据
   - 使用完毕调用 `frame_queue_free(IMAGE_MJPEG, frame)` 释放
   - **优势**：无需编码，CPU占用低

3. **显示路径（YUV消费）**
   - LCD显示模块从YUV队列获取数据
   - 通过 `frame_queue_get_frame(IMAGE_YUV, timeout)` 获取YUV帧
   - 经过视频处理管道：

     * 软件/硬件旋转（支持0°/90°/180°/270°）
     * 图像缩放（适配LCD分辨率）
     * 像素格式转换（RGB565/RGB888等）

   - 显示到LCD屏幕
   - 使用完毕调用 `frame_queue_free(IMAGE_YUV, frame)` 释放

4. **性能优势**
   - **硬件双输出**：DVP内部一次处理，同时输出MJPEG和YUV
   - **零延迟共享**：两路数据独立，互不影响
   - **完全并行**：显示和传输完全独立，无竞争
   - **无需解码**：LCD直接使用YUV，无需MJPEG解码
   - **最低CPU占用**：适合对性能要求高的场景

5. **Frame Queue管理**
   - **MJPEG Frame Queue**：管理MJPEG帧，网络传输消费
   - **YUV Frame Queue**：管理YUV帧，LCD显示消费
   - 两个队列完全独立

### 4.3 两种方案对比

.. list-table:: 两种方案对比
   :header-rows: 1

  * - 对比项
    - 方案一：单输出MJPEG
    - 方案二：双输出MJPEG+YUV
  * - 摄像头输出
    - MJPEG压缩流
    - 硬件双输出：MJPEG + YUV
  * - 使用的Frame Queue
    - MJPEG Queue
    - MJPEG Queue + YUV Queue
  * - YUV数据管理
    - 解码后直接内存申请（不用队列）
    - DVP硬件输出到YUV Queue
  * - 图传格式
    - MJPEG
    - MJPEG
  * - 是否需要解码
    - 需要MJPEG解码（显示）
    - 不需要解码（直接用YUV）
  * - 是否需要编码
    - 不需要（直接传MJPEG）
    - 不需要（直接传MJPEG）
  * - CPU占用
    - 中等（仅MJPEG解码）
    - 低（无需解码）
  * - 端到端延迟
    - 中等（需解码显示）
    - 最低（硬件直出）
  * - 网络带宽
    - 2-4Mbps（MJPEG）
    - 2-4Mbps（MJPEG）
  * - 适用场景
    - USB摄像头或单输出DVP
    - 板载双输出DVP摄像头

### 4.4 与doorbell项目的对比

.. list-table:: 与doorbell项目的对比
   :header-rows: 1

   * - 对比项
     - doorviewer项目
     - doorbell项目
   * - 图传格式
     - MJPEG
     - H264
   * - 网络带宽
     - 2-4Mbps
     - 0.5-2Mbps
   * - 兼容性
     - 更好（MJPEG通用）
     - 需要H264解码器
   * - 压缩效率
     - 低
     - 高
   * - 编码需求
     - 无需编码（直传MJPEG）
     - 需要H264编码（UVC方案）
   * - CPU占用
     - 低（无需编码）
     - 高（需要编码，UVC方案）
   * - 适用场景
     - 局域网传输、兼容性要求高
     - 广域网传输、带宽受限

### 4.5 关键技术特性

#### 4.5.1 帧队列配置

.. list-table:: 帧队列配置
   :header-rows: 1

   * - 格式
     - 帧数量
     - 主要用途
     - 使用场景
   * - MJPEG
     - 4个
     - 摄像头输出，支持网络传输和解码消费
     - 方案一：单输出；方案二：双输出
   * - YUV
     - 3个
     - DVP摄像头硬件输出，支持显示消费
     - 方案二：DVP双输出
   * - H264
     - 6个
     - 可选配置（如需H264功能）
     - 扩展功能

**重要说明**：

**方案一（单输出）**：
- ✅ MJPEG Frame Queue：摄像头输出，直接网络传输
- ❌ YUV数据：解码后**直接内存申请**（不使用YUV Frame Queue）

**方案二（双输出）**：
- ✅ MJPEG Frame Queue：DVP硬件直接输出MJPEG
- ✅ YUV Frame Queue：DVP硬件直接输出YUV
- 📌 关键优势：DVP内部硬件一次处理，同时输出两路数据

#### 4.5.2 性能优化
- **零拷贝**：支持多消费者共享同一帧数据
- **自动复用**：frame buffer自动回收复用
- **队列管理**：空闲队列和就绪队列分离
- **超时机制**：支持阻塞和非阻塞获取

#### 4.5.3 典型性能指标

**方案一（单输出）性能**：
- **MJPEG网络传输**：864x480@30fps，带宽2-4Mbps
- **MJPEG解码**：硬解延迟<33ms
- **端到端延迟**：<200ms（WiFi正常条件下）
- **CPU占用**：中等（仅MJPEG解码用于显示）

**方案二（双输出）性能**：
- **MJPEG网络传输**：864x480@15fps，带宽1-2Mbps
- **YUV显示**：硬件直出，零延迟
- **端到端延迟**：<150ms（WiFi正常条件下，最优）
- **CPU占用**：极低（仅YUV处理）

**网络传输**：
- 支持2Mbps~4Mbps码率（MJPEG）
- 支持CS2/TCP/UDP多种协议
- 兼容性好，无需H264解码器


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
 *        - format: 图像格式 (0:MJPEG)
 *        - protocol: 传输协议
 *        - rotate: 旋转角度
 * 
 * @return int 操作结果
 *         - BK_OK: 成功
 *         - BK_FAIL: 失败
 * 
 * @note 此函数会：
 *       1. 初始化frame queue用于图像帧缓存管理
 *          - Frame_buffer: frame_queue_init_all
 *       2. 根据摄像头类型(UVC或DVP)分别调用相应的开启函数
 *          - DVP: dvp_camera_turn_on
 *            * 单输出模式：输出MJPEG到MJPEG Frame Queue
 *            * 双输出模式：同时输出MJPEG和YUV到各自的Frame Queue
 *          - UVC: uvc_camera_turn_on
 *            * 输出MJPEG到MJPEG Frame Queue
 *       3. 配置图像旋转处理（如果显示控制器已初始化）
 *          - ROTATE: bk_video_pipeline_open_rotate
 *       4. **关键特性**：本项目图传使用MJPEG格式，无需H264编码
 * 
 * @note 内存管理方式：
 *       - 方案一（单输出）：MJPEG使用队列，解码后YUV直接申请内存
 *       - 方案二（双输出）：MJPEG和YUV都使用Frame Queue管理
 * 
 * @note 与doorbell项目的区别：
 *       - doorviewer：图传MJPEG，无需H264编码器
 *       - doorbell：图传H264，需要H264编码器
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
 *       1. 根据摄像头类型调用相应的关闭函数
 *          - UVC: uvc_camera_turn_off()
 *            * 断开UVC设备连接
 *            * 释放MJPEG解码器资源（如果用于显示）
 *          - DVP: dvp_camera_turn_off()
 *            * 停止DVP硬件输出（MJPEG或MJPEG+YUV）
 *            * 关闭摄像头电源
 *       2. 释放摄像头相关资源，包括：
 *          - 关闭摄像头硬件
 *          - 删除摄像头控制器
 *          - 取消flash操作通知注册
 * 
 * @note 与doorbell项目的区别：
 *       - doorviewer：无需关闭H264编码器（不使用H264）
 *       - doorbell：需要关闭H264编码器
 * 
 * @warning 调用此函数前应确保摄像头已正确开启，否则可能导致资源泄漏
 * @see doorbell_camera_turn_on()
 */
int doorbell_camera_turn_off(void);
```

### 5.2 图传API

#### 5.2.1 doorbell_video_transfer_turn_on
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
 *        - 通过WiFi传输框架开启视频帧传输
 *        - **关键特性**：传输格式为MJPEG
 *        - 从MJPEG Frame Queue获取数据
 *        - 通过CS2/TCP/UDP协议传输
 * 
 * @note 数据来源：
 *        - 方案一：从MJPEG Frame Queue获取摄像头直接输出的MJPEG数据
 *        - 方案二：从MJPEG Frame Queue获取DVP硬件输出的MJPEG数据
 * 
 * @note 与doorbell项目的区别：
 *        - doorviewer：传输MJPEG格式，带宽2-4Mbps，兼容性好
 *        - doorbell：传输H264格式，带宽0.5-2Mbps，压缩率高
 * 
 * @warning 调用此函数前应确保摄像头已正确开启
 * 
 * @see doorbell_video_transfer_turn_off()
 */
int doorbell_video_transfer_turn_on(void);
```

#### 5.2.2 doorbell_video_transfer_turn_off
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
 *        - 清理视频传输相关资源
 *        - 根据配置关闭CS2图像定时器
 *        - 释放MJPEG传输相关资源
 * 
 * @warning 调用此函数前应确保视频传输功能已正确开启
 * 
 * @see doorbell_video_transfer_turn_on()
 */
int doorbell_video_transfer_turn_off(void);
```

### 5.3 显示API

#### 5.3.1 doorbell_display_turn_on
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
 *       1. 初始化frame queue用于图像帧缓存管理
 *       2. 检查显示设备是否已开启，如果已开启则返回EVT_STATUS_ALREADY
 *       3. 根据设备ID获取LCD设备配置
 *       4. 根据LCD类型(RGB/MCU8080)创建相应的显示控制器
 *       5. 创建视频处理管道并配置旋转参数
 *       6. 打开显示控制器和LCD背光
 *       7. 设置设备信息结构体中的LCD相关参数
 * 
 * @note 数据来源和内存管理：
 *       - 方案一（单输出）：
 *         * MJPEG解码器从MJPEG Frame Queue获取数据
 *         * 解码后YUV直接申请内存（不使用YUV Frame Queue）
 *         * 视频处理管道直接使用解码器提供的YUV数据
 *       - 方案二（双输出）：
 *         * 从YUV Frame Queue获取DVP硬件输出的YUV帧
 *         * 无需MJPEG解码，直接使用YUV数据
 *         * 显示后通过frame_queue_free释放
 * 
 * @warning 如果设备初始化失败，会清理已分配的资源
 * @see doorbell_display_turn_off()
 */
int doorbell_display_turn_on(display_parameters_t *parameters);
```

#### 5.3.2 doorbell_display_turn_off
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
 *       4. 关闭显示控制器
 *       5. 删除显示控制器句柄
 *       6. 重置设备信息结构体中的LCD相关参数
 * 
 * @note 资源释放：
 *       - 方案一（单输出）：释放MJPEG解码器资源
 *       - 方案二（双输出）：无需释放解码器资源（不使用）
 * 
 * @warning 此函数会释放所有显示相关的资源
 * @see doorbell_display_turn_on()
 */
int doorbell_display_turn_off(void);
```

### 5.4 音频API

#### 5.4.1 doorbell_audio_turn_on
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
 *        - 初始化音频编码器/解码器（默认G711）
 *        - 初始化音频读取和写入句柄
 *        - 启动音频处理流程
 * 
 * @warning 调用此函数前应确保音频参数正确配置
 * 
 * @see doorbell_audio_turn_off()
 */
int doorbell_audio_turn_on(audio_parameters_t *parameters);
```

#### 5.4.2 doorbell_audio_turn_off
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

### 5.5 帧缓冲区队列管理API

本项目使用标准版帧队列管理，支持MJPEG、YUV和H264格式的帧缓存。主要特性：
- **双队列结构**：空闲队列（free list）和就绪队列（ready list）
- **自动复用**：frame buffer自动回收复用
- **阻塞与非阻塞**：支持阻塞和非阻塞获取
- **多格式支持**：支持MJPEG/YUV/H264等格式

#### 5.5.1 frame_queue_init_all
```c
/**
 * @brief 初始化所有图像格式的帧队列数据结构
 * 
 * @return bk_err_t 初始化结果
 *         - BK_OK: 所有队列初始化成功
 *         - BK_FAIL: 任一队列初始化失败
 * 
 * @note 此函数负责初始化所有支持的图像格式的帧队列，主要功能包括：
 *        - 初始化MJPEG格式的帧队列（4个帧缓存）
 *        - 初始化YUV格式的帧队列（3个帧缓存）
 *        - 初始化H264格式的帧队列（6个帧缓存，可选）
 *        - 创建空闲队列和就绪队列
 *        - 检查每个队列的初始化结果
 *        - 任一队列初始化失败则整体返回失败
 * 
 * @warning 调用此函数前应确保系统资源充足
 * 
 * @see frame_queue_deinit_all()
 */
bk_err_t frame_queue_init_all(void);
```

#### 5.5.2 frame_queue_deinit_all
```c
/**
 * @brief 释放所有图像格式的帧队列数据结构
 * 
 * @return bk_err_t 释放结果
 *         - BK_OK: 所有队列释放成功
 * 
 * @note 此函数负责释放所有支持的图像格式的帧队列，主要功能包括：
 *        - 释放MJPEG格式的帧队列
 *        - 释放YUV格式的帧队列
 *        - 释放H264格式的帧队列（如果使用）
 *        - 清理所有队列中的帧缓存资源
 *        - 反初始化所有队列结构
 * 
 * @warning 调用此函数前应确保所有队列已正确初始化
 * 
 * @see frame_queue_init_all()
 */
bk_err_t frame_queue_deinit_all(void);
```

#### 5.5.3 frame_queue_malloc
```c
/**
 * @brief 从指定图像格式的帧队列中申请一个帧缓存
 * 
 * @param format 图像格式
 *        - IMAGE_MJPEG: MJPEG格式
 *        - IMAGE_YUV: YUV格式
 *        - IMAGE_H264: H264格式（可选）
 * 
 * @param size 申请的帧大小（字节）
 *        - 指定需要申请的帧缓存大小
 * 
 * @return frame_buffer_t* 申请结果
 *         - 成功时返回申请的帧缓存指针
 *         - 失败时返回NULL
 * 
 * @note 此函数负责从空闲队列中申请帧缓存，主要功能包括：
 *        - 根据图像格式确定队列索引
 *        - 检查队列是否已初始化
 *        - 从空闲队列（free list）中获取帧缓存
 *        - 如果现有buffer大小不足，则重新分配
 *        - 设置帧的初始状态和属性
 *        - 返回申请的帧缓存指针
 * 
 * @warning 调用此函数前应确保队列已正确初始化
 * @warning 申请后必须调用frame_queue_complete或取消操作
 * 
 * @see frame_queue_complete()
 * @see frame_queue_free()
 */
frame_buffer_t *frame_queue_malloc(image_format_t format, uint32_t size);
```

#### 5.5.4 frame_queue_complete
```c
/**
 * @brief 将填充好的帧放入就绪队列
 * 
 * @param format 图像格式
 *        - IMAGE_MJPEG: MJPEG格式
 *        - IMAGE_YUV: YUV格式
 *        - IMAGE_H264: H264格式（可选）
 * 
 * @param frame 填充好的帧缓存指针
 *        - 指向需要放入就绪队列的帧缓存
 * 
 * @return bk_err_t 操作结果
 *         - BK_OK: 放入成功
 *         - BK_FAIL: 放入失败
 * 
 * @note 此函数将填充好的帧放入就绪队列，主要功能包括：
 *        - 根据图像格式确定队列索引
 *        - 检查队列是否已初始化
 *        - 构造帧消息结构
 *        - 将帧缓存放入就绪队列（ready list）
 *        - 如果放入失败则释放帧缓存
 * 
 * @warning 调用此函数前应确保队列已正确初始化
 * @warning 此函数通常在填充完数据后由生产者调用
 * 
 * @see frame_queue_malloc()
 * @see frame_queue_get_frame()
 */
bk_err_t frame_queue_complete(image_format_t format, frame_buffer_t *frame);
```

#### 5.5.5 frame_queue_get_frame
```c
/**
 * @brief 从就绪队列中获取一个帧缓存
 * 
 * @param format 图像格式
 *        - IMAGE_MJPEG: MJPEG格式
 *        - IMAGE_YUV: YUV格式
 *        - IMAGE_H264: H264格式（可选）
 * 
 * @param timeout 超时时间（毫秒）
 *        - 0: 非阻塞模式，立即返回
 *        - >0: 阻塞模式，等待指定毫秒数
 *        - RTOS_WAIT_FOREVER: 永久等待
 * 
 * @return frame_buffer_t* 获取结果
 *         - 成功时返回获取的帧缓存指针
 *         - 失败时返回NULL
 * 
 * @note 此函数从就绪队列中获取帧缓存，主要功能包括：
 *        - 根据图像格式确定队列索引
 *        - 检查队列是否已初始化
 *        - 从就绪队列（ready list）中获取帧缓存
 *        - 支持超时等待机制
 *        - 返回获取的帧缓存指针
 * 
 * @warning 调用此函数前应确保队列已正确初始化
 * @warning 获取的帧使用完毕后必须调用frame_queue_free释放
 * 
 * @see frame_queue_complete()
 * @see frame_queue_free()
 */
frame_buffer_t *frame_queue_get_frame(image_format_t format, uint32_t timeout);
```

#### 5.5.6 frame_queue_free
```c
/**
 * @brief 释放帧缓存，并将其放回空闲队列
 * 
 * @param format 图像格式
 *        - IMAGE_MJPEG: MJPEG格式
 *        - IMAGE_YUV: YUV格式
 *        - IMAGE_H264: H264格式（可选）
 * 
 * @param frame 要释放的帧缓存指针
 *        - 指向需要释放的帧缓存
 * 
 * @return void
 * 
 * @note 此函数负责释放帧缓存并回收资源，主要功能包括：
 *        - 根据图像格式确定队列索引
 *        - 检查队列是否已初始化
 *        - 根据图像格式选择合适的释放函数
 *        - 释放帧缓存的数据内存
 *        - 构造消息并放回空闲队列（free list）
 *        - 支持MJPEG/YUV/H264等格式的帧释放
 * 
 * @warning 调用此函数前应确保队列已正确初始化
 * @warning 不要重复释放同一帧
 * 
 * @see frame_queue_malloc()
 * @see frame_queue_get_frame()
 */
void frame_queue_free(image_format_t format, frame_buffer_t *frame);
```

### 5.6 帧队列使用示例

#### 5.6.1 生产者示例（摄像头采集MJPEG）
```c
// 1. 初始化帧队列
frame_queue_init_all();

// 2. 分配帧缓存
frame_buffer_t *frame = frame_queue_malloc(IMAGE_MJPEG, 100*1024);
if (frame == NULL) {
    // 分配失败处理
    return;
}

// 3. 填充MJPEG数据到frame->frame
// ... camera capture MJPEG data ...

// 4. 填充成功，放入就绪队列
frame_queue_complete(IMAGE_MJPEG, frame);
```

#### 5.6.2 消费者示例（MJPEG网络传输）
```c
// 1. 获取帧（阻塞等待）
frame_buffer_t *frame = frame_queue_get_frame(IMAGE_MJPEG, RTOS_WAIT_FOREVER);
if (frame) {
    // 2. 使用帧数据进行网络传输
    // ... send MJPEG frame data ...

    // 3. 使用完毕，释放帧
    frame_queue_free(IMAGE_MJPEG, frame);
}
```

#### 5.6.3 双输出方案示例
```c
// DVP硬件同时输出MJPEG和YUV

// 消费者1：网络传输MJPEG
frame_buffer_t *mjpeg_frame = frame_queue_get_frame(IMAGE_MJPEG, 100);
if (mjpeg_frame) {
    // 传输MJPEG数据
    wifi_send(mjpeg_frame->frame, mjpeg_frame->length);
    frame_queue_free(IMAGE_MJPEG, mjpeg_frame);
}

// 消费者2：LCD显示YUV
frame_buffer_t *yuv_frame = frame_queue_get_frame(IMAGE_YUV, 100);
if (yuv_frame) {
    // 显示YUV数据
    lcd_display(yuv_frame->frame, yuv_frame->width, yuv_frame->height);
    frame_queue_free(IMAGE_YUV, yuv_frame);
}
```

## 6 注意事项

### 6.1 硬件配置注意事项

1. 默认使用GPIO_28控制USB的LDO，拉高上电，注意GPIO冲突问题
2. 默认使用GPIO_13控制LCD的LDO，拉高上电，注意GPIO冲突问题
3. 默认使用GPIO_7控制LCD的背光，拉高有效，注意GPIO冲突问题

### 6.2 帧队列使用注意事项

1. **必须先初始化**：调用 `frame_queue_init_all()` 初始化所有队列
2. **正确释放**：获取的帧使用后必须调用 `frame_queue_free()` 释放
3. **避免重复释放**：不要对同一帧重复调用free
4. **阻塞与非阻塞**：根据场景选择合适的timeout参数

### 6.3 内存管理注意事项

**方案一（单输出MJPEG）**：
  - ✅ MJPEG帧：使用MJPEG Frame Queue管理
  - ❌ 解码后的YUV数据：直接内存申请（不使用YUV Frame Queue）
  - ❌ 视频处理管道的输出：直接内存申请

**方案二（双输出MJPEG+YUV）**：
  - ✅ MJPEG帧：使用MJPEG Frame Queue管理
  - ✅ YUV帧：使用YUV Frame Queue管理
  - 📌 关键优势：DVP硬件一次处理，同时输出两路数据

### 6.4 与doorbell项目的主要区别

.. list-table:: 特性对比
   :header-rows: 1

   * - 特性
     - doorviewer
     - doorbell
   * - 图传格式
     - MJPEG
     - H264
   * - 带宽占用
     - 2-4Mbps
     - 0.5-2Mbps
   * - H264编码器
     - 不需要
     - UVC方案需要
   * - 兼容性
     - 更好（MJPEG通用）
     - 需要H264解码器
   * - CPU占用
     - 低（无需编码）
     - 高（软件编码，UVC方案）
   * - 适用场景
     - 局域网、兼容性优先
     - 广域网、带宽受限

## 7 系统架构

项目采用模块化设计，主要包含以下模块：

### 7.1 核心模块

1. **WiFi模块**
   - 负责网络连接和数据传输
   - 支持STA/AP模式切换
   - 支持CS2/TCP/UDP多种传输协议
   - **特性**：直接传输MJPEG数据，无需H264编码

2. **媒体处理模块**
   - 处理图像采集（UVC/DVP摄像头）
   - MJPEG解码（用于LCD显示）
   - 帧缓冲区队列管理
   - **特性**：图传使用MJPEG格式，CPU占用低

3. **显示模块**
   - 管理LCD显示
   - 视频处理管道（旋转、缩放、格式转换）
   - 支持RGB/MCU屏幕

4. **蓝牙模块**
   - 提供蓝牙通信功能
   - WiFi配网功能
   - A2DP/HFP音频功能

5. **音频模块**
   - 音频采集和播放
   - 音频编解码（默认G711）
   - AEC回声消除

### 7.2 数据流架构

```
摄像头采集 → MJPEG Frame Queue → 双路消费
                                   ├─ 网络传输（MJPEG）
                                   └─ 本地显示（解码为YUV）
```

各模块之间通过明确的API接口进行交互，保证了系统的可维护性和扩展性。

## 8 配置说明

项目的主要配置选项位于Kconfig文件中，可以通过修改配置来启用或禁用特定功能：

### 8.1 主要配置项

- **摄像头类型**：UVC或DVP
- **图像分辨率**：默认864x480
- **图传格式**：MJPEG（固定）
- **LCD屏幕类型**：RGB/MCU
- **音频编解码**：G711/G726/OPUS等

## 9 故障排除

### 9.1 常见问题及解决方案

#### 9.1.1 摄像头无法识别

- 检查UVC摄像头连接是否正确，USB接口是否松动
- 确保摄像头电源供应正常，检查USB LDO（GPIO_28）是否拉高
- 确认摄像头驱动是否正确加载，可通过日志查看初始化过程
- 尝试更换兼容的UVC摄像头模块

#### 9.1.2 显示异常

- 检查LCD连接是否正确，排线是否插紧
- 确认LCD LDO（GPIO_13）和背光（GPIO_7）是否正常工作
- 检查LCD型号是否与配置匹配
- 方案一：检查MJPEG解码器是否正常工作
- 方案二：检查YUV帧队列是否正常

#### 9.1.3 WiFi连接失败

- 确认WiFi名称和密码输入正确
- 确保使用的是2.4G WiFi网络（不支持5G WiFi）
- 检查设备与路由器的距离是否过远
- 尝试通过蓝牙配网功能重新连接

#### 9.1.4 视频传输卡顿

- 检查网络连接质量，确保带宽充足（MJPEG需要2-4Mbps）
- 尝试降低视频分辨率和帧率设置
- 检查帧缓冲区管理是否存在异常
- 使用日志查看MJPEG帧队列的状态
- **注意**：MJPEG格式带宽占用较大，确保WiFi信号强度足够

#### 9.1.5 内存不足

- 检查帧缓冲区配置是否合理
- 确保正确释放使用完的帧（调用frame_queue_free）
- 避免重复释放同一帧
- 检查是否存在内存泄漏

## 10 性能优化建议

### 10.1 网络传输优化

- 根据网络带宽调整图像分辨率和帧率
- 优先使用5G以下的2.4G WiFi（信号覆盖更好）
- 考虑使用CS2协议提高传输效率

### 10.2 显示性能优化

- 方案二（双输出）性能最优，无需MJPEG解码
- 使用硬件旋转代替软件旋转
- 合理配置视频处理管道参数

### 10.3 内存优化

- 合理配置帧缓冲区数量（MJPEG: 4个，YUV: 3个）
- 及时释放不再使用的帧
- 监控帧队列状态，避免积压

## 11 总结

doorviewer项目的核心特点：
- ✅ **MJPEG图传**：兼容性好，无需H264编解码器
- ✅ **低CPU占用**：无需软件H264编码
- ✅ **双输出支持**：DVP硬件同时输出MJPEG和YUV
- ⚠️ **带宽占用较大**：适合局域网场景
- ⚠️ **压缩率较低**：相比H264占用更多带宽

**选择建议**：
- **局域网应用**：选择doorviewer（MJPEG），兼容性好
- **广域网应用**：选择doorbell（H264），带宽占用小
