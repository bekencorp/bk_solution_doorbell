# Doorbell Project Development Guide

* [中文](./README_CN.md)

## 1 Project Overview

This project is a smart doorbell and lock solution based on the BK7258 chip, implementing image data transmission via WiFi, LCD screen display, and real-time bidirectional audio communication. The project integrates rich multimedia processing capabilities, network communication functions, and user interface display, suitable for developing smart doorbell and lock devices. Unlike the doorbell project, this supports camera MJPEG image transmission and LCD display, focusing on visualization features.

## 2 Features

### 2.1 WiFi Communication
- Supports STA mode to connect to existing WiFi networks
- Supports AP mode to create WiFi hotspots for BK7258 device connections
- Supports TCP/UDP protocols for image data transmission
- Supports CS2 real-time network transmission

### 2.2 Multimedia Processing
- Supports UVC camera control and image capture, default format is MJPEG, resolution 864x480, frame rate 30fps
- Supports software or hardware decoding, automatically selected by the system
- Supports H.264 encoding function using hardware encoding
- Supports default three types of frame buffer pool management
- Supports various LCD screen displays, RGB screen or MCU screen, default uses RGB screen (st7701sn)
- Supports multiple audio codec algorithms, default uses G711 encoding
- Supports multiple transmission protocols for real-time audio and video data transmission

### 2.3 Display Functions
- LCD screen display
- LVGL graphics library support (optional)
- AVI video playback (optional)

### 2.4 Bluetooth Functions
- Basic Bluetooth functionality
- A2DP audio reception
- HFP hands-free calling
- BLE functionality
- WiFi network configuration

## 3 Quick Start

### 3.1 Hardware Preparation
- BK7258 development board
- LCD screen
- UVC/DVP camera module
- Onboard speaker/mic, or UAC
- Power supply and connection cables

### 3.2 Compilation and Flashing

Compilation process reference: `Doorbell Solution <../../index.html>`_

Flashing process reference: `Flashing Code <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.0.1/get-started/index.html#id7>`_

Compiled flashing bin file path: ``projects/doorbell/build/bk7258/doorbell/package/all-app.bin``

### 3.3 Basic Operation Process
1. Power on the device
2. Download IOT application to test device (Android), download address: <https://dl.bekencorp.com/apk/BekenIot.apk>
3. Create an account and complete login
4. Open IOT application on test device, add device, select: `Video Doorbell`, DL devices exist from 01-18 and DEBUG, recommended to first select `BK7258_DL_01` for trial use. After entering, it details the peripherals used, including UVC camera, H.264 encoder, SD card storage, LCD screen, voice functions, etc.
5. `Start Adding`, select non-5G WiFi, after successful connection, click next, start network configuration via Bluetooth
6. Check scanned device Bluetooth broadcasts, click on the matching IP address to connect, will automatically complete 100% network configuration
7. After network configuration is complete, UVC camera will automatically open, and network image transmission will start, transmission format is H.264, image resolution is 864x480
8. Open other peripherals, can be controlled on the IOT application.

## 4 Doorbell Video Stream Solutions

This project supports two camera solutions, both using multi-consumer frame queue architecture for efficient video stream processing and distribution.

### 4.1 Solution 1: UVC Camera + MJPEG + LCD Display + H264 Network Transmission

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         UVC Camera (MJPEG Output)                           │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
                    ┌──────────────────────────────┐
                    │  MJPEG Frame Queue (V2)      │
                    │  - Multi-consumer support    │
                    │  - Auto reference counting   │
                    │  - consumer_mask: 0x02       │
                    └──────────────┬───────────────┘
                                   │
                                   ▼
                        ┌─────────────────────┐
                        │  Decoder Consumer   │
                        │  (CONSUMER_DECODER) │
                        │  ID: 0x02           │
                        └──────────┬──────────┘
                                   │
                                   │ MJPEG Decoding
                                   ▼
                        ┌─────────────────────┐
                        │   YUV Buffer        │
                        │   (Direct malloc)   │
                        │   Not queue managed │
                        └──────────┬──────────┘
                                   │
                    ┌──────────────┴──────────────┐
                    │                             │
                    ▼                             ▼
         ┌─────────────────────┐      ┌────────────────────┐
         │  Video Pipeline     │      │  H264 Encoder      │
         │  Processing:        │      │  Pipeline          │
         │  1. Rotate(optional)│      │                    │
         │  2. Scale(optional) │      │  Input: Raw YUV    │
         │  3. Format convert  │      │  Output: H264      │
         └──────────┬──────────┘      └────────┬───────────┘
                    │                          │
                    │ Processed image          │ H264 Encoding
                    │ (Direct memory)          │
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
                                      │  Network Transfer  │
                                      │  (CS2/TCP/UDP)     │
                                      └────────┬───────────┘
                                               │
                                               ▼
                                      ┌────────────────────┐
                                      │  Remote Device     │
                                      │  Decode & Display  │
                                      └────────────────────┘
```

**Process Description:**

1. **MJPEG Capture and Distribution**
   - UVC camera directly outputs MJPEG compressed stream (default 864x480@30fps)
   - MJPEG frames are put into MJPEG Frame Queue via `frame_queue_v2_complete()`
   - Decoder registers as consumer (CONSUMER_DECODER), gets MJPEG frames via `frame_queue_v2_get_frame()`

2. **MJPEG Decoding**
   - Decoder decodes MJPEG to YUV format
   - **YUV data uses direct memory allocation, does NOT use YUV Frame Queue**
   - Automatically selects hardware or software decoder
   - Decoded YUV buffer is directly passed to subsequent modules

3. **Local Display Path**

  - YUV buffer goes through video processing pipeline:

    * Software/hardware rotation (supports 0°/90°/180°/270°)
    * Image scaling (adapts to LCD resolution)
    * Pixel format conversion (RGB565/RGB888, etc.)

  - **Processed image data also uses direct memory allocation**
  - Finally displayed on screen via LCD driver

4. **Network Transmission Path**
   - Decoded YUV buffer is directly fed to H264 encoder
   - H264 encoder performs hardware encoding
   - H264 frames are put into H264 Frame Queue via `frame_queue_v2_complete()`
   - WiFi transfer module registers as H264 consumer (CONSUMER_TRANSMISSION)
   - Gets H264 frames via `frame_queue_v2_get_frame()`
   - Transmits to remote device via CS2/TCP/UDP protocols

5. **Memory Management Description**
   - **MJPEG Frame Queue**: Managed by V2 queue, supports multi-consumer
   - **YUV Buffer**: Decoder directly allocates and frees memory, does not use queue
   - **Processed Image**: Video pipeline directly allocates and frees memory
   - **H264 Frame Queue**: Managed by V2 queue, supports network transmission consumer

### 4.2 Solution 2: DVP Camera + Hardware Dual Output (YUV + H264)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│              DVP Camera (Hardware Dual Output: YUV + H264)                  │
│              Internal hardware module directly outputs two streams          │
└────────────────────┬───────────────────────────┬────────────────────────────┘
                     │                           │
                     │ YUV Output                │ H264 Output
                     ▼                           ▼
      ┌──────────────────────────┐   ┌──────────────────────────┐
      │  YUV Frame Queue (V2)    │   │  H264 Frame Queue (V2)   │
      │  - Multi-consumer support│   │  - Multi-consumer support│
      │  - consumer_mask: 0x02   │   │  - consumer_mask: 0x01   │
      └──────────┬───────────────┘   └──────────┬───────────────┘
                 │                              │
                 ▼                              ▼
      ┌─────────────────────┐         ┌─────────────────────┐
      │  LCD Display        │         │  WiFi Transfer      │
      │  Consumer (0x02)    │         │  Consumer (0x01)    │
      └──────────┬──────────┘         └────────┬────────────┘
                 │                              │
                 │ Video Processing:            │
                 │ 1. Rotate(optional)          │
                 │ 2. Scale(optional)           │
                 │ 3. Format convert            │
                 ▼                              ▼
      ┌─────────────────────┐         ┌─────────────────────┐
      │  LCD Screen Display │         │  Network Transfer   │
      │                     │         │  (CS2/TCP/UDP)      │
      └─────────────────────┘         └────────┬────────────┘
                                               │
                                               ▼
                                      ┌─────────────────────┐
                                      │  Remote Device      │
                                      │  Decode & Display   │
                                      └─────────────────────┘
```

**Process Description:**

1. **DVP Hardware Dual Output**
   - DVP camera has integrated hardware module
   - Simultaneously outputs two streams:

     * YUV raw data → Put into YUV Frame Queue via `frame_queue_v2_complete()`
     * H264 encoded data → Put into H264 Frame Queue via `frame_queue_v2_complete()`

   - **No software encoder needed**, hardware directly completes H264 encoding

2. **Display Path (YUV Consumer)**
   - LCD display module registers as YUV consumer (CONSUMER_DECODER = 0x02)
   - Gets YUV frames via `frame_queue_v2_get_frame(IMAGE_YUV, CONSUMER_DECODER, timeout)`
   - Goes through video processing pipeline:

     * Software/hardware rotation (supports 0°/90°/180°/270°)
     * Image scaling (adapts to LCD resolution)
     * Pixel format conversion (RGB565/RGB888, etc.)

   - Displayed on LCD screen
   - After use, releases via `frame_queue_v2_release_frame(IMAGE_YUV, CONSUMER_DECODER, frame)`

3. **Network Transmission Path (H264 Consumer)**
   - WiFi transfer module registers as H264 consumer (CONSUMER_TRANSMISSION = 0x01)
   - Gets H264 frames via `frame_queue_v2_get_frame(IMAGE_H264, CONSUMER_TRANSMISSION, timeout)`
   - Directly transmits to remote device via CS2/TCP/UDP protocols
   - After use, releases via `frame_queue_v2_release_frame(IMAGE_H264, CONSUMER_TRANSMISSION, frame)`

4. **Performance Advantages**
   - **Hardware Dual Output**: DVP internal processing once, simultaneously outputs YUV and H264
   - **No Software Encoding Overhead**: H264 generated directly by hardware, minimal CPU usage
   - **Zero Latency Sharing**: Two streams are independent, no interference
   - **Fully Parallel**: Display and transmission are completely independent, no competition
   - **Lowest Latency**: Shortest end-to-end delay, suitable for scenarios with highest real-time requirements

5. **Frame Queue Management**
   - **YUV Frame Queue**: Manages YUV frames, LCD display consumption
   - **H264 Frame Queue**: Manages H264 frames, network transmission consumption
   - Two queues are completely independent, using V2 multi-consumer mechanism

### 4.3 Comparison of Two Solutions

.. list-table:: Comparison of Two Solutions
   :header-rows: 1

   * - Comparison Item
     - Solution 1: UVC Camera
     - Solution 2: DVP Camera
   * - Camera Output
     - MJPEG compressed stream
     - Hardware dual output: YUV + H264
   * - Frame Queues Used
     - MJPEG Queue + H264 Queue
     - YUV Queue + H264 Queue
   * - YUV Data Management
     - Direct malloc after decoding (no queue)
     - DVP hardware output to YUV Queue
   * - H264 Generation
     - Software encoding (CPU cost)
     - Hardware direct output (no CPU cost)
   * - Need Decoding
     - Need MJPEG decoding
     - No decoding needed
   * - Need Encoding
     - Need H264 software encoding
     - No encoding needed (hardware output)
   * - CPU Usage
     - High (decode + encode)
     - Low (only video processing)
   * - End-to-End Latency
     - Higher (decode+encode)
     - Lowest (hardware direct)
   * - Multi-Consumer Support
     - MJPEG and H264 support
     - YUV and H264 both support
   * - Use Case
     - USB camera
     - Onboard DVP camera

### 4.4 Key Technical Features

#### 4.4.1 Multi-Consumer Architecture
- **Consumer Types**:
  - `CONSUMER_TRANSMISSION (0x01)`: Network transmission
  - `CONSUMER_DECODER (0x02)`: Decoder
  - `CONSUMER_STORAGE (0x04)`: Storage (optional)
  - `CONSUMER_RECOGNITION (0x08)`: Recognition (optional)

#### 4.4.2 Frame Queue Configuration
.. list-table:: Frame Queue Configuration
   :header-rows: 1

   * - Format
     - Frame Count
     - Main Purpose
     - Use Case
   * - MJPEG
     - 4
     - UVC camera output, decoder consumption
     - Solution 1: UVC Camera
   * - H264
     - 6
     - Encoded H264 stream, network transmission
     - Solution 1: Software encoding; Solution 2: DVP hardware output
   * - YUV
     - 3
     - DVP camera hardware output, display consumption
     - Solution 2: DVP Camera

**Important Notes**:

**Solution 1 (UVC)**:
  - ✅ MJPEG Frame Queue: UVC camera output
  - ❌ YUV data: **Direct memory allocation** after decoding (does NOT use YUV Frame Queue)
  - ✅ H264 Frame Queue: Software encoder output

**Solution 2 (DVP)**:
  - ✅ YUV Frame Queue: DVP hardware directly outputs YUV
  - ✅ H264 Frame Queue: DVP hardware directly outputs H264 (**no software encoding needed**)
  - 📌 Key Advantage: DVP internal hardware processes once, simultaneously outputs two streams

#### 4.4.3 Performance Optimization
  - **Interrupt Safe**: Supports calling malloc/complete in interrupt context
  - **Zero Copy**: Multiple consumers share the same frame data
  - **Auto Reuse**: Frame buffer automatically recycled and reused
  - **Slow Consumer Protection**: Automatically discards old frames, does not block system

#### 4.4.4 Typical Performance Metrics

**Solution 1 (UVC) Performance**:
  - **MJPEG Decoding**: 864x480@30fps, hardware decode latency <33ms
  - **H264 Software Encoding**: 864x480@30fps, latency <50ms
  - **End-to-End Latency**: <200ms (under normal WiFi conditions)
  - **CPU Usage**: Medium (decode + encode)

**Solution 2 (DVP) Performance**:
  - **YUV Output**: Hardware direct, zero latency
  - **H264 Output**: Hardware direct, zero latency
  - **End-to-End Latency**: <100ms (under normal WiFi conditions, optimal)
  - **CPU Usage**: Very low (only YUV processing)

**Network Transmission**:
  - Supports 1Mbps~8Mbps adaptive bitrate
  - Supports CS2/TCP/UDP multiple protocols


## 5 API Reference

This section provides API interface descriptions for core functions in the project. These interfaces implement advanced function calls by encapsulating SDK.

.. note::

   It is recommended that developers do not directly call the following interfaces to implement custom solutions, but refer to the implementation methods of these interfaces and build function modules that meet their own needs by combining and encapsulating SDK interfaces.

### 5.1 Camera Management API

#### 5.1.1 doorbell_camera_turn_on
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
 *       1. Initialize frame queue V2 for image frame buffer management (multi-consumer support)
 *          - Frame_buffer: frame_queue_v2_init_all
 *       2. Call corresponding turn-on functions based on camera type (UVC or DVP)
 *          - DVP: dvp_camera_turn_on
 *            * Hardware directly outputs YUV to YUV Frame Queue
 *            * Hardware directly outputs H264 to H264 Frame Queue
 *          - UVC: uvc_camera_turn_on
 *            * Outputs MJPEG to MJPEG Frame Queue
 *       3. Initialize video processing pipeline and H264 encoder (only for UVC solution)
 *          - H264: doorbell_h264_encode_turn_on (DVP solution does not need this step)
 *       4. Configure image rotation processing (if display controller is initialized)
 *          - ROTATE: bk_video_pipeline_open_rotate
 *       5. Automatically detect and select hardware or software decoder (UVC solution only)
 * 
 * @note Memory management methods:
 *       - UVC solution: MJPEG uses queue, decoded YUV uses direct malloc, H264 software encoding uses queue
 *       - DVP solution: Hardware directly outputs YUV and H264, both use Frame Queue management (no software encoding)
 */
int doorbell_camera_turn_on(camera_parameters_t *parameters);
```

#### 5.1.2 doorbell_camera_turn_off
```c
/**
 * @brief Turn off camera device
 * 
 * @return int Operation result
 *         - BK_OK: Success
 *         - BK_FAIL: Failure
 * 
 * @note This function will:
 *       1. If current camera type is UVC camera, close H264 encoder pipeline (software encoder)
 *       2. Call corresponding turn-off functions based on camera type
 *          - UVC: uvc_camera_turn_off()
 *            * Close H264 software encoder (if opened)
 *            * Disconnect UVC device
 *            * Release MJPEG decoder resources
 *          - DVP: dvp_camera_turn_off()
 *            * Stop DVP hardware output (YUV + H264)
 *            * Turn off camera power
 *       3. Release camera-related resources, including:
 *          - Turn off camera hardware
 *          - Delete camera controller
 *          - Unregister flash operation notifications
 * 
 * @warning Before calling this function, ensure the camera is properly turned on, otherwise resource leaks may occur
 * @see doorbell_camera_turn_on()
 */
int doorbell_camera_turn_off(void);
```

### 5.2 H.264 Encoding API (UVC Solution Only)

> **Note**: This API is only used for software H264 encoding in UVC solution. DVP solution uses hardware direct output H264, does not need this encoder.

#### 5.2.1 doorbell_h264_encode_turn_on
```c
/**
 * @brief Turn on H264 encoder (UVC solution only)
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
 * @note This function is only used in UVC solution to encode decoded YUV to H264:
 *       1. Configure video processing pipeline parameters, including JPEG decoding callback function
 *       2. If video pipeline handle is empty, create new video processing pipeline
 *       3. Configure H264 encoder parameters:
 *          - Set encoding resolution (width x height)
 *          - Set frame rate to 30FPS
 *          - Configure software rotation angle
 *          - Set H264 encoding callback functions (memory allocation and encoding completion callbacks)
 *       4. Open H264 encoder pipeline
 *       5. Use doorbell_h264e_cbs callback structure:
 *          - h264e_frame_malloc: Allocate memory from H264 Frame Queue
 *          - h264e_frame_complete: Put into H264 Frame Queue after encoding
 * 
 * @note DVP solution does not call this function because DVP hardware directly outputs H264 to H264 Frame Queue
 * 
 * @warning Before calling this function, ensure the camera device is properly initialized
 * @see doorbell_h264_encode_turn_off()
 */
int doorbell_h264_encode_turn_on(camera_parameters_t *parameters);
```

#### 5.2.2 doorbell_h264_encode_turn_off
```c
/**
 * @brief Turn off H264 encoder (UVC solution only)
 * 
 * @return int Operation result
 *         - BK_OK: Success
 *         - BK_FAIL: Failure
 * 
 * @note This function is only used in UVC solution:
 *       1. Check if video pipeline handle is empty, if empty return success directly
 *       2. Call bk_video_pipeline_close_h264e() to close H264 encoder pipeline
 *       3. Record shutdown log information
 *       4. Release H264 encoder related resources
 * 
 * @note DVP solution does not call this function because it uses hardware direct output H264
 * 
 * @attention This function only turns off the H264 encoder, does not turn off the entire camera device
 * @warning Before calling this function, ensure the H264 encoder is properly turned on
 * @see doorbell_h264_encode_turn_on()
 */
int doorbell_h264_encode_turn_off(void);
```

### 5.3 Video Transmission API

#### 5.3.1 doorbell_video_transfer_turn_on
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
 *        - Register transmission task as H264 frame queue consumer (CONSUMER_TRANSMISSION)
 *        - Turn on video frame transmission through WiFi transmission framework
 *        - Configure transmission parameters based on transmission format
 * 
 * @note Data sources:
 *        - UVC solution: Gets software-encoded H264 frames from H264 Frame Queue
 *        - DVP solution: Gets hardware direct-output H264 frames from H264 Frame Queue
 * 
 * @warning Before calling this function, ensure the camera is properly turned on
 * 
 * @see doorbell_video_transfer_turn_off()
 */
int doorbell_video_transfer_turn_on(void);
```

#### 5.3.2 doorbell_video_transfer_turn_off
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
 *        - Unregister transmission task consumer (CONSUMER_TRANSMISSION)
 *        - Automatically recycle frames not accessed by this consumer
 *        - Clean up video transmission related resources
 *        - Turn off CS2 image timer based on configuration
 * 
 * @warning Before calling this function, ensure the video transmission function is properly turned on
 * 
 * @see doorbell_video_transfer_turn_on()
 */
int doorbell_video_transfer_turn_off(void);
```

### 5.4 Display API

#### 5.4.1 doorbell_display_turn_on
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
 *       1. Initialize frame queue V2 for image frame buffer management (multi-consumer support)
 *       2. Check if display device is already turned on, if already on return EVT_STATUS_ALREADY
 *       3. Get LCD device configuration based on device ID
 *       4. Create corresponding display controller based on LCD type (RGB/MCU8080)
 *       5. Create video processing pipeline and configure rotation parameters
 *       6. If using DVP camera, register YUV frame queue display consumer
 *       7. Open display controller and LCD backlight
 *       8. Set LCD related parameters in device information structure
 * 
 * @note Data source and memory management:
 *       - DVP solution:
 *         * Get YUV frames from YUV Frame Queue (DVP hardware output)
 *         * Register as YUV consumer (CONSUMER_DECODER)
 *         * Release via frame_queue_v2_release_frame after display
 *       - UVC solution:
 *         * Decoder directly allocates YUV memory (does NOT use YUV Frame Queue)
 *         * Video processing pipeline directly uses YUV data provided by decoder
 *         * Directly free memory after display (does NOT call frame_queue_v2_release_frame)
 * 
 * @warning If device initialization fails, will clean up allocated resources
 * @see doorbell_display_turn_off()
 */
int doorbell_display_turn_on(display_parameters_t *parameters);
```

#### 5.4.2 doorbell_display_turn_off
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
 *       4. If using DVP camera, unregister YUV frame queue display consumer (CONSUMER_DECODER)
 *       5. Automatically recycle YUV frames not accessed by this consumer (DVP solution only)
 *       6. Turn off display controller
 *       7. Delete display controller handle
 *       8. Reset LCD related parameters in device information structure
 * 
 * @note Consumer unregistration:
 *       - DVP solution: Call frame_queue_v2_unregister_consumer to unregister YUV consumer
 *       - UVC solution: No need to unregister consumer (decoded YUV data does not use frame queue)
 * 
 * @warning This function will release all display-related resources
 * @see doorbell_display_turn_on()
 */
int doorbell_display_turn_off(void);
```

### 5.5 Audio API

#### 5.5.1 doorbell_audio_turn_on
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

#### 5.5.2 doorbell_audio_turn_off
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

### 5.6 Frame Buffer Queue Management API (V2 Multi-Consumer Version)

This project uses frame queue V2 version, supporting multi-consumer mode, reference counting, and automatic memory management. Main features:
- **Multi-Consumer Support**: Multiple consumers can access the same frame simultaneously
- **Reference Count Management**: Automatically tracks frame usage status
- **Slow Consumer Protection**: Automatically discards old frames, does not block fast consumers
- **Dynamic Registration**: Supports dynamic consumer registration and unregistration
- **Memory Reuse**: Frame buffer automatically reused, reduces memory allocation

#### Consumer Type Definitions
```c
#define CONSUMER_TRANSMISSION   (1 << 0)  // Video transmission task
#define CONSUMER_DECODER        (1 << 1)  // Decoder task
#define CONSUMER_STORAGE        (1 << 2)  // Storage task
#define CONSUMER_RECOGNITION    (1 << 3)  // Recognition task
#define CONSUMER_CUSTOM_1       (1 << 4)  // Custom task 1
#define CONSUMER_CUSTOM_2       (1 << 5)  // Custom task 2
#define CONSUMER_CUSTOM_3       (1 << 6)  // Custom task 3
#define CONSUMER_CUSTOM_4       (1 << 7)  // Custom task 4
```

#### 5.6.1 frame_queue_v2_init_all
```c
/**
 * @brief Initialize frame queue data structures for all image formats (V2 version)
 * 
 * @return bk_err_t Initialization result
 *         - BK_OK: All queues initialized successfully
 *         - BK_FAIL: Any queue initialization failed
 * 
 * @note This function initializes V2 version frame queue system, supports:
 *        - MJPEG format frame queue (default 4 frame buffers)
 *        - H264 format frame queue (default 6 frame buffers)
 *        - YUV format frame queue (default 3 frame buffers)
 *        - Doubly-linked list management structure
 *        - Multi-consumer support (up to 8 consumers)
 *        - Thread-safe critical section protection
 * 
 * @warning Before calling this function, ensure system resources are sufficient
 * 
 * @see frame_queue_v2_deinit_all()
 */
bk_err_t frame_queue_v2_init_all(void);
```

#### 5.6.2 frame_queue_v2_deinit_all
```c
/**
 * @brief Release frame queue data structures for all image formats (V2 version)
 * 
 * @return bk_err_t Release result
 *         - BK_OK: All queues released successfully
 * 
 * @note This function is responsible for releasing all V2 frame queue resources:
 *        - Release all nodes in free_list
 *        - Release all nodes in ready_list
 *        - Release all frame buffer memory
 *        - Deinitialize semaphores
 *        - Print statistics (malloc/complete/free counts)
 * 
 * @see frame_queue_v2_init_all()
 */
bk_err_t frame_queue_v2_deinit_all(void);
```

#### 5.6.3 frame_queue_v2_register_consumer
```c
/**
 * @brief Register consumer
 * 
 * @param format Image format
 *        - IMAGE_MJPEG: MJPEG format
 *        - IMAGE_H264: H264 format
 *        - IMAGE_YUV: YUV format
 * 
 * @param consumer_id Consumer ID (CONSUMER_XXX macro definition)
 *        - Each consumer uses a unique bit identifier
 * 
 * @return bk_err_t Operation result
 *         - BK_OK: Registration successful
 *         - BK_FAIL: Registration failed
 * 
 * @note This function registers a new consumer:
 *        - Update active_consumers mask
 *        - Initialize consumer information structure
 *        - Clean up old frames with consumer_mask=0 in ready_list
 *        - After this, the consumer can access all newly generated frames
 * 
 * @warning Consumer ID must be a power of 2 (single bit)
 * 
 * @see frame_queue_v2_unregister_consumer()
 * @see frame_queue_v2_get_frame()
 */
bk_err_t frame_queue_v2_register_consumer(image_format_t format, uint32_t consumer_id);
```

#### 5.6.4 frame_queue_v2_unregister_consumer
```c
/**
 * @brief Unregister consumer
 * 
 * @param format Image format
 * @param consumer_id Consumer ID
 * 
 * @return bk_err_t Operation result
 *         - BK_OK: Unregistration successful
 *         - BK_FAIL: Unregistration failed
 * 
 * @note This function unregisters consumer and cleans up resources:
 *        - Remove this consumer from active_consumers
 *        - Update consumer_mask of all unaccessed frames in ready_list
 *        - Automatically recycle no-longer-needed frames to free_list (keep frame buffer for reuse)
 *        - Print statistics (get/release counts)
 * 
 * @warning After unregistration, the consumer should not call get_frame/release_frame
 * 
 * @see frame_queue_v2_register_consumer()
 */
bk_err_t frame_queue_v2_unregister_consumer(image_format_t format, uint32_t consumer_id);
```

#### 5.6.5 frame_queue_v2_malloc
```c
/**
 * @brief Allocate frame buffer (for producer use)
 * 
 * @param format Image format
 * @param size Requested frame size
 * 
 * @return frame_buffer_t* Allocated frame buffer pointer, NULL on failure
 * 
 * @note This function allocates frame buffer for producer:
 *        - Get free node from free_list
 *        - Reuse existing frame buffer or allocate new one
 *        - Set consumer_mask to current active_consumers
 *        - Supports interrupt context calling (protected by spinlock)
 *        - If free_list is empty, for MJPEG and YUV will automatically reuse oldest frame
 * 
 * @warning After allocation, must call complete or cancel
 * 
 * @see frame_queue_v2_complete()
 * @see frame_queue_v2_cancel()
 */
frame_buffer_t *frame_queue_v2_malloc(image_format_t format, uint32_t size);
```

#### 5.6.6 frame_queue_v2_complete
```c
/**
 * @brief Put filled frame into ready queue (for producer use)
 * 
 * @param format Image format
 * @param frame Filled frame buffer
 * 
 * @return bk_err_t Operation result
 *         - BK_OK: Put successful
 *         - BK_FAIL: Put failed
 * 
 * @note This function submits frame to consumers:
 *        - Move from free_list to ready_list
 *        - Set frame's timestamp and consumer_mask
 *        - Wake up waiting consumers via semaphore
 *        - Supports interrupt context calling
 * 
 * @warning Do not call complete repeatedly on the same frame
 * 
 * @see frame_queue_v2_malloc()
 * @see frame_queue_v2_get_frame()
 */
bk_err_t frame_queue_v2_complete(image_format_t format, frame_buffer_t *frame);
```

#### 5.6.7 frame_queue_v2_cancel
```c
/**
 * @brief Cancel allocated but failed frame (for producer use)
 * 
 * @param format Image format
 * @param frame Frame buffer to cancel
 * 
 * @return bk_err_t Operation result
 *         - BK_OK: Cancel successful
 *         - BK_FAIL: Cancel failed
 * 
 * @note Called when producer encounters error after malloc:
 *        - Reset node's in_use flag
 *        - Keep frame buffer for next reuse
 *        - Node remains in free_list
 *        - Avoid memory leak
 * 
 * @see frame_queue_v2_malloc()
 */
bk_err_t frame_queue_v2_cancel(image_format_t format, frame_buffer_t *frame);
```

#### 5.6.8 frame_queue_v2_get_frame
```c
/**
 * @brief Consumer get frame (supports multiple consumers accessing same frame simultaneously)
 * 
 * @param format Image format
 * @param consumer_id Consumer ID
 * @param timeout Timeout in milliseconds
 *        - 0: Non-blocking, return immediately
 *        - >0: Wait for specified milliseconds
 *        - BEKEN_WAIT_FOREVER: Wait forever
 * 
 * @return frame_buffer_t* Obtained frame buffer pointer, NULL on failure
 * 
 * @note Multi-consumer safe access:
 *        - Automatically skip already accessed frames
 *        - Automatically clean up expired frames with consumer_mask=0
 *        - Increase frame's reference count
 *        - Mark this consumer as accessed
 *        - Must call release_frame after use
 * 
 * @warning Must call register_consumer first to register
 * 
 * @see frame_queue_v2_register_consumer()
 * @see frame_queue_v2_release_frame()
 */
frame_buffer_t *frame_queue_v2_get_frame(image_format_t format, uint32_t consumer_id, uint32_t timeout);
```

#### 5.6.9 frame_queue_v2_release_frame
```c
/**
 * @brief Consumer release frame
 * 
 * @param format Image format
 * @param consumer_id Consumer ID
 * @param frame Frame buffer to release
 * 
 * @return void
 * 
 * @note Reference count management:
 *        - Decrease frame's reference count
 *        - When ref_count=0 and all consumers that need access have accessed:
 *          * Release frame buffer memory
 *          * Reset node state
 *          * Move node back to free_list
 *        - Thread-safe, supports multiple consumers releasing concurrently
 * 
 * @warning Do not release the same frame repeatedly
 * 
 * @see frame_queue_v2_get_frame()
 */
void frame_queue_v2_release_frame(image_format_t format, uint32_t consumer_id, frame_buffer_t *frame);
```

#### 5.6.10 frame_queue_v2_get_stats
```c
/**
 * @brief Get queue statistics
 * 
 * @param format Image format
 * @param free_count Output: number of free frames
 * @param ready_count Output: number of ready frames
 * @param total_malloc Output: total allocation count
 * @param total_complete Output: total completion count
 * @param total_free Output: total free count
 * 
 * @return bk_err_t Operation result
 *         - BK_OK: Get successful
 *         - BK_FAIL: Get failed
 * 
 * @note Used for performance monitoring and debugging
 */
bk_err_t frame_queue_v2_get_stats(image_format_t format, 
                                   uint32_t *free_count, 
                                   uint32_t *ready_count,
                                   uint32_t *total_malloc,
                                   uint32_t *total_complete,
                                   uint32_t *total_free);
```

### 5.7 Frame Queue V2 Usage Examples

#### 5.7.1 Producer Example (Camera Capture)
```c
// 1. Initialize frame queue
frame_queue_v2_init_all();

// 2. Allocate frame buffer
frame_buffer_t *frame = frame_queue_v2_malloc(IMAGE_MJPEG, 100*1024);
if (frame == NULL) {
    // Handle allocation failure
    return;
}

// 3. Fill data into frame->frame
// ... camera capture data ...

// 4. Successfully filled, put into ready queue
frame_queue_v2_complete(IMAGE_MJPEG, frame);

// Or if filling failed, cancel the frame
// frame_queue_v2_cancel(IMAGE_MJPEG, frame);
```

#### 5.7.2 Consumer Example (Video Transmission)
```c
// 1. Register as transmission consumer
frame_queue_v2_register_consumer(IMAGE_MJPEG, CONSUMER_TRANSMISSION);

// 2. Get frame (blocking wait)
frame_buffer_t *frame = frame_queue_v2_get_frame(IMAGE_MJPEG, 
                                                  CONSUMER_TRANSMISSION, 
                                                  BEKEN_WAIT_FOREVER);
if (frame) {
    // 3. Use frame data
    // ... send frame data ...
    
    // 4. After use, release frame
    frame_queue_v2_release_frame(IMAGE_MJPEG, CONSUMER_TRANSMISSION, frame);
}

// 5. Unregister consumer when closing
frame_queue_v2_unregister_consumer(IMAGE_MJPEG, CONSUMER_TRANSMISSION);
```

#### 5.7.3 Multi-Consumer Example
```c
// Scenario: Simultaneously support transmission and storage

// Consumer 1: Video transmission task
frame_queue_v2_register_consumer(IMAGE_H264, CONSUMER_TRANSMISSION);

// Consumer 2: Storage task
frame_queue_v2_register_consumer(IMAGE_H264, CONSUMER_STORAGE);

// Two consumers can access the same frame simultaneously
// Consumer 1 gets and uses
frame_buffer_t *frame1 = frame_queue_v2_get_frame(IMAGE_H264, CONSUMER_TRANSMISSION, 100);
// Transmission processing...
frame_queue_v2_release_frame(IMAGE_H264, CONSUMER_TRANSMISSION, frame1);

// Consumer 2 gets and uses (same frame)
frame_buffer_t *frame2 = frame_queue_v2_get_frame(IMAGE_H264, CONSUMER_STORAGE, 100);
// Storage processing...
frame_queue_v2_release_frame(IMAGE_H264, CONSUMER_STORAGE, frame2);

// Frame is only truly recycled after both consumers release it
```

#### 5.7.4 Slow Consumer Handling
```c
// V2 version automatically handles slow consumers:
// - Fast consumers are not blocked by slow consumers
// - Slow consumers will automatically lose some old frames
// - Slow consumers can always get the latest available frames

// Example: Transmission task processing slowly
frame_queue_v2_register_consumer(IMAGE_MJPEG, CONSUMER_TRANSMISSION);

while (running) {
    // Even if processing slowly, will skip old frames and get latest frame
    frame_buffer_t *frame = frame_queue_v2_get_frame(IMAGE_MJPEG, 
                                                      CONSUMER_TRANSMISSION, 
                                                      100);
    if (frame) {
        // Slow processing...
        slow_network_send(frame);
        frame_queue_v2_release_frame(IMAGE_MJPEG, CONSUMER_TRANSMISSION, frame);
    }
}
```

## 6 Important Notes

1. Default uses GPIO_28 to control USB LDO, pull high to power on, pay attention to GPIO conflict issues
2. Default uses GPIO_13 to control LCD LDO, pull high to power on, pay attention to GPIO conflict issues
3. Default uses GPIO_7 to control LCD backlight, pull high to enable, pay attention to GPIO conflict issues

4. **Frame Queue V2 Notes**:
   - Consumers must register first before getting frames
   - Obtained frames must call release_frame after use
   - Do not release the same frame repeatedly
   - Consumers must unregister before closing to avoid resource leaks
   - V2 version automatically reuses frame buffers, no manual memory management needed

5. **Memory Management Differences**:
   - UVC Solution (MJPEG software decode + software H264 encode):

     * ✅ MJPEG frames: Managed by MJPEG Frame Queue V2
     * ❌ Decoded YUV data: **Direct memory allocation**, does NOT use YUV Frame Queue
     * ❌ Video pipeline output: Direct memory allocation
     * ✅ H264 frames: Managed by H264 Frame Queue after software encoding
     * 📌 CPU overhead: Requires software MJPEG decode + software H264 encode
   
   - DVP Solution (Hardware dual output):

     * ✅ YUV frames: DVP hardware directly outputs to YUV Frame Queue V2
     * ✅ H264 frames: DVP hardware directly outputs to H264 Frame Queue V2
     * ❌ No software encoder needed: DVP internal hardware simultaneously generates two streams
     * 📌 CPU overhead: Minimal, hardware direct output
   
   - Key Differences:

     * YUV Frame Queue is **only used for DVP camera** hardware output
     * UVC solution's YUV data **does NOT go through queue**, uses direct memory allocation
     * DVP solution **does NOT need H264 encoder**, hardware directly outputs H264

## 7 System Architecture

The project adopts modular design, mainly including the following modules:

1. **WiFi Module**: Responsible for network connection and data transmission
2. **Media Processing Module**: Handles image capture, encoding and storage
3. **Display Module**: Manages LCD display
4. **Bluetooth Module**: Provides Bluetooth communication functionality

Each module interacts through clear API interfaces, ensuring system maintainability and scalability.

## 8 Configuration Instructions

Main configuration options of the project are located in Kconfig files, specific functions can be enabled or disabled by modifying configurations:

## 9 Troubleshooting

### 9.1 Common Issues and Solutions

#### 9.1.1 Camera Cannot Be Recognized

   - Check if UVC camera connection is correct and USB interface is not loose
   - Ensure camera power supply is normal and check if USB LDO is pulled high
   - Confirm if camera driver is correctly loaded, check initialization process through logs
   - Try replacing with a compatible UVC camera module

#### 9.1.2 Display Abnormalities

   - Check if LCD connection is correct and the cable is properly inserted
   - Confirm if LCD LDO and backlight are working normally
   - Check if LCD model matches the configuration

#### 9.1.3 WiFi Connection Failure

   - Confirm WiFi name and password are entered correctly
   - Ensure using 2.4G WiFi network (5G WiFi is not supported)
   - Check if the distance between device and router is too far
   - Try reconnecting through Bluetooth network configuration function

#### 9.1.4 Video Transmission Lag

   - Check network connection quality to ensure sufficient bandwidth
   - Try reducing video resolution and frame rate settings
   - Confirm H.264 encoder is working normally
   - Check if there are any abnormalities in frame buffer management
   - Use `frame_queue_v2_get_stats()` to view queue statistics
