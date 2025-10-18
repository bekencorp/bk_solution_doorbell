# Doorbell Project Development Guide

* [中文](./README_CN.md)

## 1 Project Overview

This project is a smart doorbell and lock solution based on the BK7258 chip, implementing image data transmission via WiFi and display on LCD screens. The project integrates rich multimedia processing capabilities, network communication functions, and user interface display, suitable for the development of smart doorbell and lock devices.

## 2 Features

### 2.1 WiFi Communication
- Supports STA mode to connect to existing WiFi networks
- Supports AP mode to create WiFi hotspots for BK7258 device connections
- Supports TCP/UDP protocol for image data transmission
- Supports CS2 real-time network transmission

### 2.2 Multimedia Processing
- Supports UVC camera control and image capture, default format is MJPEG, resolution 864x480, frame rate 30fps
- Supports software or hardware decoding, automatically selected by the system
- Supports H.264 encoding function, using hardware encoding
- Supports UVC (MJPEG) frame buffer management
- Supports H.264 frame buffer management
- Supports various LCD screen displays, RGB screen or MCU screen, default uses RGB screen (st7701sn)
- Supports multiple audio codec algorithms, default uses G711 encoding
- Supports multiple transmission protocols for real-time audio and video data transmission

### 2.3 Display Functions
- LCD screen display
- LVGL graphics library support
- AVI video playback (optional)

### 2.4 Bluetooth Functions
- Basic Bluetooth functionality
- A2DP audio reception
- HFP hands-free calling
- BLE functionality
- WiFi network configuration functionality

## 3 Quick Start

### 3.1 Hardware Preparation
- BK7258 development board
- LCD screen
- Optional: UVC camera module
- Power supply and connection cables

### 3.2 Compilation and Flashing

Compilation process reference: `Doorbell Solution <../../index.html>`_

Flashing process reference: `Flashing Code <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.0.1/get-started/index.html#id7>`_

Compiled flashing bin file path: ``projects/doorbell/build/bk7258/doorbell/package/all-app.bin``

### 3.3 Basic Operation Process
1. Power on the device
2. Download IOT application to test device (Android), download address: <https://dl.bekencorp.com/apk/BekenIot.apk>
3. Create an account and complete login
4. Open IOT application on test device, add device, select: `Video Doorbell`, DL devices exist from 01-18, and DEBUG, recommended to first select `BK7258_DL_01` for trial use. After entering, it details the peripherals used, including UVC camera, H.264 encoder, SD card storage, LCD screen, voice functions, etc.
5. `Start Adding`, select non-5G WiFi, after successful connection, click next, start network configuration via Bluetooth
6. Check scanned device Bluetooth broadcasts, click on the matching IP address to connect, will automatically complete 100% network configuration
7. After network configuration is complete, UVC camera will automatically open, and network image transmission will start, transmission format is H.264, image resolution is 864x480
8. Open other peripherals, can be controlled on the IOT application.

## 4 API Reference

This section provides API interface descriptions for core functions in the project, which implement advanced function calls by encapsulating SDK.

.. note::

   It is recommended that developers do not directly call the following interfaces to implement custom solutions, but refer to the implementation methods of these interfaces and build function modules that meet their own needs by combining and encapsulating SDK interfaces.

### 4.1 Camera Management API

#### 4.1.1 doorbell_camera_turn_on
```c
/**
 * @brief Turn on camera device
 * 
 * @param parameters Camera parameter structure pointer
 *        - id: Camera device ID (UVC_DEVICE_ID or other DVP device ID)
 *        - width: Image width
 *        - height: Image height
 *        - format: Image format (0:MJPEG, 1:H264)
 *        - protocol: Transmission protocol
 *        - rotate: Rotation angle
 * 
 * @return int Operation result
 *         - BK_OK: Success
 *         - BK_FAIL: Failure
 * 
 * @note This function will:
 *       1. Initialize frame queue for image frame buffer management
 *          - Frame_buffer: frame_queue_init_all
 *       2. Call corresponding turn-on functions based on camera type (UVC or DVP)
 *          - DVP: doorbell_dvp_camera_turn_on
 *          - UVC: doorbell_uvc_camera_turn_on
 *       3. Initialize video processing pipeline and H264 encoder
 *          - H264: doorbell_h264_encode_turn_on
 *       4. Configure image rotation processing (if display controller is initialized)
 *          - ROTATE: bk_video_pipeline_open_rotate
 */
int doorbell_camera_turn_on(camera_parameters_t *parameters);
```

#### 4.1.2 doorbell_camera_turn_off
```c
/**
 * @brief Turn off camera device
 * 
 * @return int Operation result
 *         - BK_OK: Success
 *         - BK_FAIL: Failure
 * 
 * @note This function will:
 *       1. If current camera type is UVC camera, close H264 encoder pipeline
 *       2. Call doorbell_camera_device_turn_off() to turn off camera device
 *       3. For UVC cameras:
 *          - Close H264 encoder
 *          - Record shutdown log
 *       4. For DVP cameras:
 *          - Directly turn off camera device
 *       5. Release camera-related resources, including:
 *          - Turn off camera hardware
 *          - Delete camera controller
 *          - Unregister flash operation notifications
 *          - Turn off camera power (for DVP cameras)
 *          - Disconnect UVC device (for UVC cameras)
 * 
 * @warning Before calling this function, ensure the camera is properly turned on, otherwise resource leaks may occur
 * @see doorbell_camera_turn_on()
 */
int doorbell_camera_turn_off(void);
```

### 4.2 H.264 Encoding API

#### 4.2.1 doorbell_h264_encode_turn_on
```c
/**
 * @brief Turn on H264 encoder
 * 
 * @param parameters Camera parameter structure pointer
 *        - width: Encoded image width
 *        - height: Encoded image height
 *        - rotate: Image rotation angle
 * 
 * @return int Operation result
 *         - BK_OK: Success
 *         - BK_FAIL: Failure
 * 
 * @note This function will:
 *       1. Configure video processing pipeline parameters, including JPEG decoding callback function
 *       2. If video pipeline handle is empty, create new video processing pipeline
 *       3. Configure H264 encoder parameters:
 *          - Set encoding resolution (width x height)
 *          - Set frame rate to 30FPS
 *          - Configure software rotation angle
 *          - Set H264 encoding callback functions (memory allocation and encoding completion callbacks)
 *       4. Open H264 encoder pipeline
 *       5. Use doorbell_h264e_cbs callback structure:
 *          - h264e_frame_malloc: Memory allocation callback
 *          - h264e_frame_complete: Encoding completion callback
 * 
 * @warning Before calling this function, ensure the camera device is properly initialized
 * @see doorbell_h264_encode_turn_off()
 */
int doorbell_h264_encode_turn_on(camera_parameters_t *parameters);
```

#### 4.2.2 doorbell_h264_encode_turn_off
```c
/**
 * @brief Turn off H264 encoder
 * 
 * @return int Operation result
 *         - BK_OK: Success
 *         - BK_FAIL: Failure
 * 
 * @note This function will:
 *       1. Check if video pipeline handle is empty, if empty return success directly
 *       2. Call bk_video_pipeline_close_h264e() to close H264 encoder pipeline
 *       3. Record shutdown log information
 *       4. Release H264 encoder related resources
 * 
 * @attention This function only turns off the H264 encoder, does not turn off the entire camera device
 * @warning Before calling this function, ensure the H264 encoder is properly turned on
 * @see doorbell_h264_encode_turn_on()
 */
int doorbell_h264_encode_turn_off(void);
```

### 4.3 Video Transmission API

#### 4.3.1 doorbell_video_transfer_turn_on
```c
/**
 * @brief Turn on video transmission function
 * 
 * @return int Operation result
 *         - BK_OK: Operation successful
 *         - BK_FAIL: Operation failed
 * 
 * @note This function is responsible for turning on video transmission function, main functions include:
 *        - Check if video information structure is valid
 *        - Check if camera is turned on
 *        - Verify if video transmission callback function is set
 *        - Turn on video frame transmission through WiFi transmission framework
 *        - Configure transmission parameters based on transmission format
 * 
 * @warning Before calling this function, ensure the camera is properly turned on
 * 
 * @see doorbell_video_transfer_turn_off()
 */
int doorbell_video_transfer_turn_on(void);
```

#### 4.3.2 doorbell_video_transfer_turn_off
```c
/**
 * @brief Turn off video transmission function
 * 
 * @return int Operation result
 *         - BK_OK: Operation successful
 *         - BK_FAIL: Operation failed
 * 
 * @note This function is responsible for turning off video transmission function, main functions include:
 *        - Check if video information structure is valid
 *        - Check if camera is turned on
 *        - Turn off video frame transmission through WiFi transmission framework
 *        - Clean up video transmission related resources
 *        - Turn off CS2 image timer based on configuration
 * 
 * @warning Before calling this function, ensure the video transmission function is properly turned on
 * 
 * @see doorbell_video_transfer_turn_on()
 */
int doorbell_video_transfer_turn_off(void);
```

### 4.4 Display API

#### 4.4.1 doorbell_display_turn_on
```c
/**
 * @brief Turn on display device
 * 
 * @param parameters Display parameter structure pointer
 *        - id: Display device ID
 *        - rotate_angle: Rotation angle
 *        - pixel_format: Pixel format (0:hardware rotation, 1:software rotation)
 * 
 * @return int Operation result
 *         - EVT_STATUS_OK: Success
 *         - EVT_STATUS_ERROR: Failure
 *         - EVT_STATUS_ALREADY: Device already turned on
 * 
 * @note This function will:
 *       1. Initialize frame queue for image frame buffer management
 *       2. Check if display device is already turned on, if already on return EVT_STATUS_ALREADY
 *       3. Get LCD device configuration based on device ID
 *       4. Create corresponding display controller based on LCD type (RGB/MCU8080)
 *       5. Create video processing pipeline and configure rotation parameters
 *       6. Open display controller and LCD backlight
 *       7. Set LCD related parameters in device information structure
 * 
 * @warning If device initialization fails, will clean up allocated resources
 * @see doorbell_display_turn_off()
 */
int doorbell_display_turn_on(display_parameters_t *parameters);
```

#### 4.4.2 doorbell_display_turn_off
```c
/**
 * @brief Turn off display device
 * 
 * @return int Operation result
 *         - 0: Success
 *         - EVT_STATUS_ALREADY: Device already turned off
 *         - EVT_STATUS_ERROR: Failure
 * 
 * @note This function will:
 *       1. Check if display device is already turned off, if already off return EVT_STATUS_ALREADY
 *       2. Turn off LCD backlight
 *       3. Turn off rotation function of video processing pipeline
 *       4. Turn off display controller
 *       5. Delete display controller handle
 *       6. Reset LCD related parameters in device information structure
 * 
 * @warning This function will release all display-related resources
 * @see doorbell_display_turn_on()
 */
int doorbell_display_turn_off(void);
```

### 4.5 Audio API

#### 4.5.1 doorbell_audio_turn_on
```c
/**
 * @brief Turn on audio device
 * 
 * @param parameters Audio parameter structure pointer
 *        - aec: Echo cancellation enable flag
 *        - uac: USB audio device enable flag
 *        - rmt_recorder_sample_rate: Remote recording sample rate
 *        - rmt_player_sample_rate: Remote playback sample rate
 *        - rmt_recoder_fmt: Remote recording encoding format
 *        - rmt_player_fmt: Remote playback encoding format
 * 
 * @return int Operation result
 *         - BK_OK: Operation successful
 *         - BK_FAIL: Operation failed or device already turned on
 * 
 * @note This function is responsible for turning on audio device, main functions include:
 *        - Check if audio device is already turned on
 *        - Configure audio parameters (sample rate, encoding format, etc.)
 *        - Select audio configuration method based on UAC flag
 *        - Configure AEC echo cancellation parameters
 *        - Initialize audio encoder/decoder
 *        - Initialize audio read and write handles
 *        - Start audio processing flow
 * 
 * @warning Before calling this function, ensure audio parameters are correctly configured
 * 
 * @see doorbell_audio_turn_off()
 */
int doorbell_audio_turn_on(audio_parameters_t *parameters);
```

#### 4.5.2 doorbell_audio_turn_off
```c
/**
 * @brief Turn off audio device
 * 
 * @return int Operation result
 *         - BK_OK: Operation successful
 *         - BK_FAIL: Operation failed or device already turned off
 * 
 * @note This function is responsible for turning off audio device, main functions include:
 *        - Check if audio device is already turned off
 *        - Set audio device status to off
 *        - Notify service of audio status change
 *        - Stop audio read and write operations
 *        - Deinitialize audio related handles
 *        - Clean up audio resources
 * 
 * @warning Before calling this function, ensure the audio device is properly initialized
 * 
 * @see doorbell_audio_turn_on()
 */
int doorbell_audio_turn_off(void);
```

### 4.6 Frame Buffer Queue Management API

#### 4.6.1 frame_queue_init_all
```c
/**
 * @brief Initialize frame queue data structures for all image formats
 * 
 * @return bk_err_t Initialization result
 *         - BK_OK: All queues initialized successfully
 *         - BK_FAIL: Any queue initialization failed
 * 
 * @note This function is responsible for initializing frame queues for all supported image formats, main functions include:
 *        - Initialize MJPEG format frame queue
 *        - Initialize H264 format frame queue
 *        - Initialize YUV format frame queue
 *        - Check initialization result of each queue
 *        - If any queue initialization fails, return failure overall
 * 
 * @warning Before calling this function, ensure system resources are sufficient
 * 
 * @see frame_queue_init()
 * @see frame_queue_deinit_all()
 */
bk_err_t frame_queue_init_all(void);
```

##### 4.6.2 frame_queue_deinit_all
```c
/**
 * @brief Release frame queue data structures for all image formats
 * 
 * @return bk_err_t Release result
 *         - BK_OK: All queues released successfully
 * 
 * @note This function is responsible for releasing frame queues for all supported image formats, main functions include:
 *        - Release MJPEG format frame queue
 *        - Release H264 format frame queue
 *        - Release YUV format frame queue
 *        - Clean up frame buffer resources in all queues
 *        - Deinitialize all queue structures
 * 
 * @warning Before calling this function, ensure all queues are properly initialized
 * 
 * @see frame_queue_deinit()
 * @see frame_queue_init_all()
 */
bk_err_t frame_queue_deinit_all(void);
```

##### 4.6.3 frame_queue_malloc
```c
/**
 * @brief Allocate a frame buffer from the frame queue of specified image format
 * 
 * @param format Image format
 *        - IMAGE_MJPEG: MJPEG format
 *        - IMAGE_H264: H264 format
 *        - IMAGE_YUV: YUV format
 * 
 * @param size Frame size to allocate
 *        - Specify the frame buffer size to allocate
 * 
 * @return frame_buffer_t* Allocation result
 *         - Returns allocated frame buffer pointer on success
 *         - Returns NULL on failure
 * 
 * @note This function is responsible for allocating frame buffer from frame queue, main functions include:
 *        - Determine queue index based on image format
 *        - Check if queue is initialized
 *        - Get frame buffer from free queue
 *        - Select appropriate allocation function based on image format
 *        - Set initial state and attributes of frame
 *        - Return allocated frame buffer pointer
 * 
 * @warning Before calling this function, ensure the queue is properly initialized
 * 
 * @see frame_queue_free()
 * @see frame_queue_get_frame()
 */
frame_buffer_t *frame_queue_malloc(image_format_t format, uint32_t size);
```

##### 4.6.4 frame_queue_get_frame
```c
/**
 * @brief Get a frame buffer from the ready queue of the frame queue for specified image format
 * 
 * @param format Image format
 *        - IMAGE_MJPEG: MJPEG format
 *        - IMAGE_H264: H264 format
 *        - IMAGE_YUV: YUV format
 * 
 * @param timeout Timeout in milliseconds
 *        - 0: Non-blocking mode, return immediately
 *        - >0: Blocking mode, wait for specified milliseconds
 *        - RTOS_WAIT_FOREVER: Wait forever
 * 
 * @return frame_buffer_t* Get result
 *         - Returns obtained frame buffer pointer on success
 *         - Returns NULL on failure
 * 
 * @note This function is responsible for getting frame buffer from ready queue of frame queue, main functions include:
 *        - Determine queue index based on image format
 *        - Check if queue is initialized
 *        - Get frame buffer from ready queue
 *        - Support timeout waiting mechanism
 *        - Return obtained frame buffer pointer
 * 
 * @warning Before calling this function, ensure the queue is properly initialized
 * 
 * @see frame_queue_malloc()
 * @see frame_queue_complete()
 */
frame_buffer_t *frame_queue_get_frame(image_format_t format, uint32_t timeout);
```

##### 4.6.5 frame_queue_complete
```c
/**
 * @brief Return frame buffer to the ready queue of the frame queue for specified image format
 * 
 * @param format Image format
 *        - IMAGE_MJPEG: MJPEG format
 *        - IMAGE_H264: H264 format
 *        - IMAGE_YUV: YUV format
 * 
 * @param frame Frame buffer to return to queue
 *        - Pointer to frame buffer that needs to be returned to ready queue
 * 
 * @return bk_err_t Operation result
 *         - BK_OK: Return successful
 *         - BK_FAIL: Return failed
 * 
 * @note This function is responsible for returning frame buffer to ready queue, main functions include:
 *        - Determine queue index based on image format
 *        - Check if queue is initialized
 *        - Construct frame message structure
 *        - Put frame buffer into ready queue
 *        - If return fails, release frame buffer
 * 
 * @warning Before calling this function, ensure the queue is properly initialized
 * 
 * @see frame_queue_get_frame()
 * @see frame_queue_free()
 */
bk_err_t frame_queue_complete(image_format_t format, frame_buffer_t *frame);
```

##### 4.6.6 frame_queue_free
```c
/**
 * @brief Release frame buffer based on image format and send message to free queue
 * 
 * @param format Image format
 *        - IMAGE_MJPEG: MJPEG format
 *        - IMAGE_H264: H264 format
 *        - IMAGE_YUV: YUV format
 * 
 * @param frame Frame buffer to release
 *        - Pointer to frame buffer that needs to be released
 * 
 * @return void
 * 
 * @note This function is responsible for releasing frame buffer and recycling resources, main functions include:
 *        - Determine queue index based on image format
 *        - Check if queue is initialized
 *        - Select appropriate release function based on image format
 *        - Release frame buffer resources
 *        - Construct message and return to free queue
 *        - Support release of MJPEG/H264 encoded frames and YUV display frames
 * 
 * @warning Before calling this function, ensure the queue is properly initialized
 * 
 * @see frame_queue_malloc()
 * @see frame_queue_complete()
 */
void frame_queue_free(image_format_t format, frame_buffer_t *frame);
```

## 5 Important Notes

1. Default uses GPIO_28 to control USB LDO, pull high to power on, pay attention to GPIO conflict issues
2. Default uses GPIO_13 to control LCD LDO, pull high to power on, pay attention to GPIO conflict issues
3. Default uses GPIO_7 to control LCD backlight, pull high to enable, pay attention to GPIO conflict issues

## 6 System Architecture

The project adopts modular design, mainly including the following modules:

1. **WiFi Module**: Responsible for network connection and data transmission
2. **Media Processing Module**: Handles image capture, encoding and storage
3. **Display Module**: Manages LCD display
4. **Bluetooth Module**: Provides Bluetooth communication functionality

Each module interacts through clear API interfaces, ensuring system maintainability and scalability.

## 7 Configuration Instructions

Main configuration options of the project are located in Kconfig files, specific functions can be enabled or disabled by modifying configurations:

## 8 Troubleshooting

### 8.1 Common Issues and Solutions

#### 8.1.1 Camera Cannot Be Recognized

   - Check if UVC camera connection is correct and USB interface is not loose
   - Ensure camera power supply is normal and check if USB LDO is pulled high
   - Confirm if camera driver is correctly loaded, check initialization process through logs
   - Try replacing with a compatible UVC camera module

#### 8.1.2 Display Abnormalities

   - Check if LCD connection is correct and the cable is properly inserted
   - Confirm if LCD LDO and backlight are working normally
   - Check if LCD model matches the configuration

#### 8.1.3 WiFi Connection Failure

   - Confirm WiFi name and password are entered correctly
   - Ensure using 2.4G WiFi network (5G WiFi is not supported)
   - Check if the distance between device and router is too far
   - Try reconnecting through Bluetooth network configuration function

#### 8.1.4 Video Transmission Lag

   - Check network connection quality to ensure sufficient bandwidth
   - Try reducing video resolution and frame rate settings
   - Confirm H.264 encoder is working normally
   - Check if there are any abnormalities in frame buffer management
