Doorbell项目开发指南
==========================================


:link_to_translation:`en:[English]`

1 项目概述
------------------


本项目是一个基于BK7258芯片的智能门铃解决方案，实现了通过WiFi传输图像数据并在LCD屏幕上显示的功能。项目集成了丰富的多媒体处理能力、网络通信功能和用户界面显示，适用于智能出行设备的开发。

2 功能特性
------------------


2.1 WiFi通信
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

* 支持STA模式连接到现有WiFi网络
* 支持AP模式创建WiFi热点供BK7258设备连接
* 支持TCP/UDP协议传输图像数据
* 支持CS2实时网络传输

2.2 多媒体处理
,,,,,,,,,,,,,,,,,,,,,,,,,,,

* 支持UVC摄像头控制与图像采集，默认格式为MJPEG，分辨率为864x480，帧率为30fps
* 支持软件或硬件解码，系统内部自动选择，用户不需要思考
* 支持H.264编码功能，编码时使用硬编码
* 支持UVC（MJPEG）帧缓冲区管理
* 支持H.264帧缓冲区管理
* 支持多种LCD屏幕显示，RGB屏或MCU屏，默认使用RBGB屏（st7701sn）
* 支持多种音频编解码算法，默认使用G711编码
* 支持多种传输协议，实时传输音视频数据

2.3 显示功能
,,,,,,,,,,,,,,,,,,,,,,,,

* LCD屏幕显示
* LVGL图形库支持
* AVI视频播放（可选）

2.4 蓝牙功能
,,,,,,,,,,,,,,,,,,,,,,,,

* 蓝牙基础功能
* A2DP音频接收
* HFP免提通话
* BLE功能
* WiFi配网功能

3 快速开始
------------------


3.1 硬件准备
,,,,,,,,,,,,,,,,,,,,,,,,

* BK7258开发板
* LCD屏幕
* 可选：UVC摄像头模块
* 电源和连接线

3.2 编译和烧录
,,,,,,,,,,,,,,,,,,,,,,,,,,,


编译流程参考 `Doorbell 解决方案 <../../index.html>`_

烧录流程参考 `烧录代码 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.0.1/get-started/index.html#id7>`_

编译生成的烧录bin文件路径：``projects/doorbell/build/bk7258/doorbell/package/all-app.bin``

3.3 基本操作流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

1. 设备上电启动
2. 测试机（Android）下载IOT应用到设备，下载地址： <https://dl.bekencorp.com/apk/BekenIot.apk>
3. 自行创建账号，并完成登录
4. 测试机打开IOT应用，添加设备，选择： ``可视门铃`` ，DL设备存在01-18，和DEBUG，建议先选择 ``BK7258_DL_01`` 进行尝试使用。点进去后里面详细介绍了使用的外设，包括UVC摄像头、H.264编码器、SD卡存储、LCD屏幕、语音功能等。
5. ``开始添加``，选择非5G的WiFi，连接成功后，点击下一步，开始通过蓝牙进行配网
6. 检查扫描到的设备蓝牙广播，点击IP地址匹配的进行连接，会自动完成100%的配网
7. 配网完成之后会自动打开UVC摄像头，且打开网络图传，传输的格式是H.264，图像的分辨率为864X480
8. 打开其他外设，可以在IOT应用上进行控制。

4 API参考
---------------------


下面这些API接口都是模拟用户的操作，通过封装SDK的接口进行实现的

.. note::

   建议客户不要直接调用下面的接口去实现自己的方案，也需要参照这些接口的实现方式，通过组合封装SDK接口来实现自己的功能

4.1 摄像头管理API
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,


4.1.1 doorbell_camera_turn_on
.......................................................................................

.. code-block:: c

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
    *       1. 初始化frame queue用于图像帧缓存管理
    *          - Frame_buffer: frame_queue_init_all
    *       2. 根据摄像头类型(UVC或DVP)分别调用相应的开启函数
    *          - DVP: doorbell_dvp_camera_turn_on
    *          - UVC: doorbell_uvc_camera_turn_on
    *       3. 初始化视频处理管道和H264编码器
    *          - H264: doorbell_h264_encode_turn_on
    *       4. 配置图像旋转处理（如果显示控制器已初始化）
    *          - ROTATE: bk_video_pipeline_open_rotate
    */
   int doorbell_camera_turn_on(camera_parameters_t *parameters);



4.1.2 doorbell_camera_turn_off
..........................................................................................

.. code-block:: c

   /**
    * @brief 关闭摄像头设备
    * 
    * @return int 操作结果
    *         - BK_OK: 成功
    *         - BK_FAIL: 失败
    * 
    * @note 此函数会：
    *       1. 如果当前摄像头类型为UVC摄像头，关闭H264编码器管道
    *       2. 调用doorbell_camera_device_turn_off()关闭摄像头设备
    *       3. 对于UVC摄像头：
    *          - 关闭H264编码器
    *          - 记录关闭日志
    *       4. 对于DVP摄像头：
    *          - 直接关闭摄像头设备
    *       5. 释放摄像头相关资源，包括：
    *          - 关闭摄像头硬件
    *          - 删除摄像头控制器
    *          - 取消flash操作通知注册
    *          - 关闭摄像头电源（对于DVP摄像头）
    *          - 断开UVC设备连接（对于UVC摄像头）
    * 
    * @warning 调用此函数前应确保摄像头已正确开启，否则可能导致资源泄漏
    * @see doorbell_camera_turn_on()
    */
   int doorbell_camera_turn_off(void);



4.2 H.264编码API
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,


4.2.1 doorbell_h264_encode_turn_on
......................................................................................................

.. code-block:: c

   /**
    * @brief 开启H264编码器
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
    * @note 此函数会：
    *       1. 配置视频处理管道参数，包括JPEG解码回调函数
    *       2. 如果视频管道句柄为空，创建新的视频处理管道
    *       3. 配置H264编码器参数：
    *          - 设置编码分辨率（width x height）
    *          - 设置帧率为30FPS
    *          - 配置软件旋转角度
    *          - 设置H264编码回调函数（内存分配和编码完成回调）
    *       4. 打开H264编码器管道
    *       5. 使用doorbell_h264e_cbs回调结构体：
    *          - h264e_frame_malloc: 内存分配回调
    *          - h264e_frame_complete: 编码完成回调
    * 
    * @warning 调用此函数前应确保摄像头设备已正确初始化
    * @see doorbell_h264_encode_turn_off()
    */
   int doorbell_h264_encode_turn_on(camera_parameters_t *parameters);



4.2.2 doorbell_h264_encode_turn_off
.........................................................................................................

.. code-block:: c

   /**
    * @brief 关闭H264编码器
    * 
    * @return int 操作结果
    *         - BK_OK: 成功
    *         - BK_FAIL: 失败
    * 
    * @note 此函数会：
    *       1. 检查视频管道句柄是否为空，如果为空则直接返回成功
    *       2. 调用bk_video_pipeline_close_h264e()关闭H264编码器管道
    *       3. 记录关闭日志信息
    *       4. 释放H264编码器相关资源
    * 
    * @attention 此函数仅关闭H264编码器，不会关闭整个摄像头设备
    * @warning 调用此函数前应确保H264编码器已正确开启
    * @see doorbell_h264_encode_turn_on()
    */
   int doorbell_h264_encode_turn_off(void);



4.3 图传API
,,,,,,,,,,,,,,,,,,,,,,,,,,,


4.3.1 doorbell_video_transfer_turn_on
...............................................................................................................

.. code-block:: c

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
    *        - 根据传输格式配置传输参数
    * 
    * @warning 调用此函数前应确保摄像头已正确开启
    * 
    * @see doorbell_video_transfer_turn_off()
    */
   int doorbell_video_transfer_turn_on(void);



4.3.2 doorbell_video_transfer_turn_off
..................................................................................................................

.. code-block:: c

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
    * 
    * @warning 调用此函数前应确保视频传输功能已正确开启
    * 
    * @see doorbell_video_transfer_turn_on()
    */
   int doorbell_video_transfer_turn_off(void);



4.4 显示API
,,,,,,,,,,,,,,,,,,,,,,,,,,,


4.4.1 doorbell_display_turn_on
..........................................................................................

.. code-block:: c

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
    * @warning 如果设备初始化失败，会清理已分配的资源
    * @see doorbell_display_turn_off()
    */
   int doorbell_display_turn_on(display_parameters_t *parameters);



4.4.2 doorbell_display_turn_off
.............................................................................................

.. code-block:: c

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
    * @warning 此函数会释放所有显示相关的资源
    * @see doorbell_display_turn_on()
    */
   int doorbell_display_turn_off(void);



4.5 音频API
,,,,,,,,,,,,,,,,,,,,,,,,,,,


4.5.1 doorbell_audio_turn_on
....................................................................................

.. code-block:: c

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



4.5.2 doorbell_audio_turn_off
.......................................................................................

.. code-block:: c

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



4.6 帧缓冲区队列管理API
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,


4.6.1 frame_queue_init_all
..............................................................................

.. code-block:: c

   /**
    * @brief 初始化所有图像格式的帧队列数据结构
    * 
    * @return bk_err_t 初始化结果
    *         - BK_OK: 所有队列初始化成功
    *         - BK_FAIL: 任一队列初始化失败
    * 
    * @note 此函数负责初始化所有支持的图像格式的帧队列，主要功能包括：
    *        - 初始化MJPEG格式的帧队列
    *        - 初始化H264格式的帧队列
    *        - 初始化YUV格式的帧队列
    *        - 检查每个队列的初始化结果
    *        - 任一队列初始化失败则整体返回失败
    * 
    * @warning 调用此函数前应确保系统资源充足
    * 
    * @see frame_queue_init()
    * @see frame_queue_deinit_all()
    */
   bk_err_t frame_queue_init_all(void);



4.6.2 frame_queue_deinit_all
************************************************************************************

.. code-block:: c

   /**
    * @brief 释放所有图像格式的帧队列数据结构
    * 
    * @return bk_err_t 释放结果
    *         - BK_OK: 所有队列释放成功
    * 
    * @note 此函数负责释放所有支持的图像格式的帧队列，主要功能包括：
    *        - 释放MJPEG格式的帧队列
    *        - 释放H264格式的帧队列
    *        - 释放YUV格式的帧队列
    *        - 清理所有队列中的帧缓存资源
    *        - 反初始化所有队列结构
    * 
    * @warning 调用此函数前应确保所有队列已正确初始化
    * 
    * @see frame_queue_deinit()
    * @see frame_queue_init_all()
    */
   bk_err_t frame_queue_deinit_all(void);



4.6.3 frame_queue_malloc
************************************************************************

.. code-block:: c

   /**
    * @brief 从指定图像格式的帧队列中申请一个帧缓存
    * 
    * @param format 图像格式
    *        - IMAGE_MJPEG: MJPEG格式
    *        - IMAGE_H264: H264格式
    *        - IMAGE_YUV: YUV格式
    * 
    * @param size 申请的帧大小
    *        - 指定需要申请的帧缓存大小
    * 
    * @return frame_buffer_t* 申请结果
    *         - 成功时返回申请的帧缓存指针
    *         - 失败时返回NULL
    * 
    * @note 此函数负责从帧队列中申请帧缓存，主要功能包括：
    *        - 根据图像格式确定队列索引
    *        - 检查队列是否已初始化
    *        - 从空闲队列中获取帧缓存
    *        - 根据图像格式选择合适的申请函数
    *        - 设置帧的初始状态和属性
    *        - 返回申请的帧缓存指针
    * 
    * @warning 调用此函数前应确保队列已正确初始化
    * 
    * @see frame_queue_free()
    * @see frame_queue_get_frame()
    */
   frame_buffer_t *frame_queue_malloc(image_format_t format, uint32_t size);



4.6.4 frame_queue_get_frame
*********************************************************************************

.. code-block:: c

   /**
    * @brief 从指定图像格式的帧队列的就绪队列中获取一个帧缓存
    * 
    * @param format 图像格式
    *        - IMAGE_MJPEG: MJPEG格式
    *        - IMAGE_H264: H264格式
    *        - IMAGE_YUV: YUV格式
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
    * @note 此函数负责从帧队列的就绪队列中获取帧缓存，主要功能包括：
    *        - 根据图像格式确定队列索引
    *        - 检查队列是否已初始化
    *        - 从就绪队列中获取帧缓存
    *        - 支持超时等待机制
    *        - 返回获取的帧缓存指针
    * 
    * @warning 调用此函数前应确保队列已正确初始化
    * 
    * @see frame_queue_malloc()
    * @see frame_queue_complete()
    */
   frame_buffer_t *frame_queue_get_frame(image_format_t format, uint32_t timeout);



4.6.5 frame_queue_complete
******************************************************************************

.. code-block:: c

   /**
    * @brief 将帧缓存放回指定图像格式的帧队列的就绪队列
    * 
    * @param format 图像格式
    *        - IMAGE_MJPEG: MJPEG格式
    *        - IMAGE_H264: H264格式
    *        - IMAGE_YUV: YUV格式
    * 
    * @param frame 要放回队列的帧缓存
    *        - 指向需要放回就绪队列的帧缓存指针
    * 
    * @return bk_err_t 操作结果
    *         - BK_OK: 放回成功
    *         - BK_FAIL: 放回失败
    * 
    * @note 此函数负责将帧缓存放回就绪队列，主要功能包括：
    *        - 根据图像格式确定队列索引
    *        - 检查队列是否已初始化
    *        - 构造帧消息结构
    *        - 将帧缓存放入就绪队列
    *        - 如果放回失败则释放帧缓存
    * 
    * @warning 调用此函数前应确保队列已正确初始化
    * 
    * @see frame_queue_get_frame()
    * @see frame_queue_free()
    */
   bk_err_t frame_queue_complete(image_format_t format, frame_buffer_t *frame);



4.6.6 frame_queue_free
******************************************************************

.. code-block:: c

   /**
    * @brief 根据图像格式释放帧缓存，并将消息发送到空闲队列
    * 
    * @param format 图像格式
    *        - IMAGE_MJPEG: MJPEG格式
    *        - IMAGE_H264: H264格式
    *        - IMAGE_YUV: YUV格式
    * 
    * @param frame 要释放的帧缓存
    *        - 指向需要释放的帧缓存指针
    * 
    * @return void
    * 
    * @note 此函数负责释放帧缓存并回收资源，主要功能包括：
    *        - 根据图像格式确定队列索引
    *        - 检查队列是否已初始化
    *        - 根据图像格式选择合适的释放函数
    *        - 释放帧缓存资源
    *        - 构造消息并放回空闲队列
    *        - 支持MJPEG/H264编码帧和YUV显示帧的释放
    * 
    * @warning 调用此函数前应确保队列已正确初始化
    * 
    * @see frame_queue_malloc()
    * @see frame_queue_complete()
    */
   void frame_queue_free(image_format_t format, frame_buffer_t *frame);



5 注意事项
------------------


1. 默认使用GPIO_28控制USB的LDO，拉高上电，注意GPIO冲突问题
2. 默认使用GPIO_13控制LCD的LDO，拉高上电，注意GPIO冲突问题
3. 默认使用GPIO_13控制LCD的背光，拉高有效，注意GPIO冲突问题

6 系统架构
------------------


项目采用模块化设计，主要包含以下模块：

1. **WiFi模块**：负责网络连接和数据传输
2. **媒体处理模块**：处理图像采集、编码和存储
3. **显示模块**：管理LCD显示
4. **蓝牙模块**：提供蓝牙通信功能

各模块之间通过明确的API接口进行交互，保证了系统的可维护性和扩展性。

7 配置说明
------------------


项目的主要配置选项位于Kconfig文件中，可以通过修改配置来启用或禁用特定功能：

8 故障排除
------------------


8.1 常见问题及解决方案
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,


8.1.1 摄像头无法识别
.......................................


   - 检查UVC摄像头连接是否正确
   - 确保摄像头电源供应正常
   - 确认摄像头驱动是否正确加载