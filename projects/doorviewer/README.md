# Doorviewer Project Development Guide

* [中文](./README_CN.md)

## 1 Project Overview

This project is a smart doorbell and lock solution based on the BK7258 chip, implementing MJPEG image data transmission via WiFi and display on LCD screens. The project integrates rich multimedia processing capabilities, network communication functions, and user interface display, suitable for the development of smart doorbell and lock devices.

**Key Differences**:
- **doorviewer project**: Uses MJPEG format for video transmission, larger bandwidth usage, better compatibility
- **doorbell project**: Uses H264 format for video transmission, smaller bandwidth usage, higher compression efficiency

## 2 Features

### 2.1 WiFi Communication
- Supports STA mode to connect to existing WiFi networks
- Supports AP mode to create WiFi hotspots for BK7258 device connections
- Supports TCP/UDP protocol for image data transmission
- Supports CS2 real-time network transmission
- **Main Feature**: Uses MJPEG format for video transmission, no need for H264 encoding, better compatibility

### 2.2 Multimedia Processing
- Supports UVC camera control and image capture, default format is MJPEG, resolution 864x480, frame rate 30fps
- Supports DVP camera control and image capture, default format is MJPEG, resolution 864x480, frame rate 15fps
- Supports DVP camera dual output mode (MJPEG + YUV simultaneous output)
- Supports software or hardware MJPEG decoding, automatically selected by the system
- Supports MJPEG/YUV/H264 frame buffer management
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
- WiFi network configuration functionality

## 3 Quick Start

### 3.1 Hardware Preparation
- BK7258 development board
- LCD screen
- UVC/DVP camera module
- Onboard speaker/mic, or UAC
- Power supply and connection cables

### 3.2 Compilation and Flashing

Compilation process reference: `Doorviewer Solution <../../index.html>`_

Flashing process reference: `Flashing Code <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.0.1/get-started/index.html#id7>`_

Compiled flashing bin file path: ``projects/doorviewer/build/bk7258/doorviewer/package/all-app.bin``

### 3.3 Basic Operation Process
1. Power on the device
2. Download IOT application to test device (Android), download address: <https://dl.bekencorp.com/apk/BekenIot.apk>
3. Create an account and complete login
4. Open IOT application on test device, add device, select: `Video Doorbell`
5. `Start Adding`, select non-5G WiFi, after successful connection, click next, start network configuration via Bluetooth
6. Check scanned device Bluetooth broadcasts, click on the matching IP address to connect, will automatically complete 100% network configuration
7. After network configuration is complete, the camera will automatically open, and network video transmission will start, transmission format is MJPEG, image resolution is 864x480
8. Open other peripherals, can be controlled on the IOT application

## 4 doorviewer Video Stream Solutions

This project supports two camera solutions, with the core difference being **video transmission uses MJPEG format**, no need for H264 encoding, better compatibility but larger bandwidth usage.

### 4.1 Solution 1: UVC/DVP Camera + MJPEG Output + MJPEG Network Transmission

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    UVC/DVP Camera (MJPEG Output)                             │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                                       ▼
                    ┌──────────────────────────────┐
                    │  MJPEG Frame Queue           │
                    │  - frame_queue_malloc        │
                    │  - Cache MJPEG compressed    │
                    │    data                      │
                    └──────────────┬───────────────┘
                                   │
                    ┌──────────────┴──────────────┐
                    │                             │
                    ▼                             ▼
         ┌─────────────────────┐      ┌────────────────────┐
         │  WiFi Transfer      │      │  Video Pipeline    │
         │  (Direct MJPEG      │      │  (Local Display)   │
         │   transmission)     │      │                    │
         │  Path 1: Network    │      │  Path 2: LCD       │
         └──────────┬──────────┘      └────────┬───────────┘
                    │                           │
                    │ MJPEG stream              │ MJPEG decode +
                    ▼                           │ YUV processing
         ┌─────────────────────┐               ▼
         │  Network Transfer   │      ┌────────────────────┐
         │  (CS2/TCP/UDP)      │      │  Video Pipeline:   │
         └──────────┬──────────┘      │  1. MJPEG Decode   │
                    │                 │  2. Rotate (opt)   │
                    ▼                 │  3. Scale (opt)    │
         ┌─────────────────────┐      │  4. Format Conv    │
         │  Remote Device      │      └────────┬───────────┘
         │  MJPEG Decode &     │               │
         │  Display            │               ▼
         └─────────────────────┘      ┌────────────────────┐
                                      │  LCD Display       │
                                      │  Driver            │
                                      └────────────────────┘
```

**Process Description:**

1. **MJPEG Capture**
   - UVC/DVP camera directly outputs MJPEG compressed stream (default 864x480@30fps)
   - MJPEG frames are placed into MJPEG frame queue via `frame_queue_complete()`
   - Supports dual consumption: network transmission and local decode display

2. **Network Transmission Path (Path 1)**
   - **Key Feature**: Directly transmits MJPEG stream, no need for H264 encoding
   - WiFi transfer module gets data from MJPEG frame queue
   - Gets MJPEG frames via `frame_queue_get_frame()`
   - Directly transmits MJPEG data via CS2/TCP/UDP protocols
   - Remote device receives and performs MJPEG decode display
   - **Advantage**: Good compatibility, no need for H264 codec
   - **Disadvantage**: Larger bandwidth usage (about 2-4Mbps)

3. **Local Display Path (Path 2)**
   - Video pipeline gets MJPEG frames from frame queue
   - Video pipeline integrates MJPEG decoding and YUV processing:

     * MJPEG decode to YUV format
     * Software/hardware rotation (supports 0°/90°/180°/270°)
     * Image scaling (adapt to LCD resolution)
     * Pixel format conversion (RGB565/RGB888, etc.)

   - Processed image is displayed on screen via LCD driver

4. **Memory Management**
   - **MJPEG Frame Queue**: Uses frame_queue management, supports multi-consumer access
   - **YUV Processing**: Video pipeline handles decode and processing internally
   - **Processed Image**: Video pipeline manages memory internally

### 4.2 Solution 2: DVP Camera Dual Output (MJPEG + YUV) + MJPEG Network Transmission

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                  DVP Camera (Dual Output: MJPEG + YUV)                       │
│                  Hardware module directly outputs two streams                │
└────────────────────┬───────────────────────────┬────────────────────────────┘
                     │                           │
                     │ MJPEG output              │ YUV output
                     ▼                           ▼
      ┌──────────────────────────┐   ┌──────────────────────────┐
      │  MJPEG Frame Queue       │   │  YUV Frame Queue         │
      │  - frame_queue_malloc    │   │  - frame_queue_malloc    │
      └──────────┬───────────────┘   └──────────┬───────────────┘
                 │                              │
                 ▼                              ▼
      ┌─────────────────────┐         ┌─────────────────────┐
      │  WiFi Transfer      │         │  LCD Display        │
      │  (Direct MJPEG      │         │  (Direct YUV)       │
      │   transmission)     │         │                     │
      └──────────┬──────────┘         └────────┬────────────┘
                 │                              │
                 │ MJPEG stream                 │ Video processing:
                 │                              │ 1. Rotate (opt)
                 │                              │ 2. Scale (opt)
                 │                              │ 3. Format conv
                 ▼                              ▼
      ┌─────────────────────┐         ┌─────────────────────┐
      │  Network Transfer   │         │  LCD Screen         │
      │  (CS2/TCP/UDP)      │         │  Display            │
      └────────┬────────────┘         └─────────────────────┘
               │
               ▼
      ┌─────────────────────┐
      │  Remote Device      │
      │  MJPEG Decode &     │
      │  Display            │
      └─────────────────────┘
```

**Process Description:**

1. **DVP Hardware Dual Output**
   - DVP camera has integrated hardware module
   - Simultaneously outputs two streams:

     * MJPEG compressed data → Placed into MJPEG Frame Queue via `frame_queue_complete()`
     * YUV raw data → Placed into YUV Frame Queue via `frame_queue_complete()`

   - **No software decoder needed**: LCD directly uses YUV data

2. **Network Transmission Path (MJPEG Consumption)**
   - WiFi transfer module gets data from MJPEG queue
   - Gets MJPEG frames via `frame_queue_get_frame(IMAGE_MJPEG, timeout)`
   - Directly transmits MJPEG data via CS2/TCP/UDP protocols
   - Releases via `frame_queue_free(IMAGE_MJPEG, frame)` after use
   - **Advantage**: No encoding needed, low CPU usage

3. **Display Path (YUV Consumption)**
   - LCD display module gets data from YUV queue
   - Gets YUV frames via `frame_queue_get_frame(IMAGE_YUV, timeout)`
   - Passes through video processing pipeline:

     * Software/hardware rotation (supports 0°/90°/180°/270°)
     * Image scaling (adapt to LCD resolution)
     * Pixel format conversion (RGB565/RGB888, etc.)

   - Displays on LCD screen
   - Releases via `frame_queue_free(IMAGE_YUV, frame)` after use

4. **Performance Advantages**
   - **Hardware dual output**: DVP processes once internally, outputs two streams simultaneously
   - **Zero-latency sharing**: Two streams are independent, no interference
   - **Fully parallel**: Display and transmission are completely independent, no competition
   - **No decoding needed**: LCD directly uses YUV, no MJPEG decoding
   - **Lowest CPU usage**: Suitable for high performance requirements

5. **Frame Queue Management**
   - **MJPEG Frame Queue**: Manages MJPEG frames, network transmission consumption
   - **YUV Frame Queue**: Manages YUV frames, LCD display consumption
   - Two queues are completely independent

### 4.3 Comparison of Two Solutions

.. list-table:: Comparison of Two Solutions
   :header-rows: 1

   * - Comparison Item
     - Solution 1: Single Output MJPEG
     - Solution 2: Dual Output MJPEG+YUV
   * - Camera Output
     - MJPEG compressed stream
     - Hardware dual output: MJPEG + YUV
   * - Frame Queues Used
     - MJPEG Queue
     - MJPEG Queue + YUV Queue
   * - YUV Data Management
     - Video pipeline internal (no queue)
     - DVP hardware output to YUV Queue
   * - Video Transmission Format
     - MJPEG
     - MJPEG
   * - Decoding Needed
     - MJPEG decode (via pipeline)
     - No decode (direct YUV)
   * - Encoding Needed
     - No (direct MJPEG)
     - No (direct MJPEG)
   * - CPU Usage
     - Medium (MJPEG decode in pipeline)
     - Low (no decode)
   * - End-to-end Latency
     - Medium (decode in pipeline)
     - Lowest (hardware direct)
   * - Network Bandwidth
     - 2-4Mbps (MJPEG)
     - 2-4Mbps (MJPEG)
   * - Use Case
     - USB camera or single output DVP
     - Onboard dual output DVP camera

### 4.4 Comparison with doorbell Project

.. list-table:: Comparison with doorbell Project
   :header-rows: 1

   * - Comparison Item
     - doorviewer Project
     - doorbell Project
   * - Video Transmission Format
     - MJPEG
     - H264
   * - Network Bandwidth
     - 2-4Mbps
     - 0.5-2Mbps
   * - Compatibility
     - Better (MJPEG universal)
     - Needs H264 decoder
   * - Compression Efficiency
     - Low
     - High
   * - Encoding Requirement
     - No encoding (direct MJPEG)
     - H264 encoding needed (UVC solution)
   * - CPU Usage
     - Low (no encoding)
     - High (software encoding, UVC solution)
   * - Use Case
     - LAN transmission, compatibility priority
     - WAN transmission, bandwidth limited

### 4.5 Key Technical Features

#### 4.5.1 Frame Queue Configuration

.. list-table:: Frame Queue Configuration
   :header-rows: 1

   * - Format
     - Frame Count
     - Main Use
     - Use Case
   * - MJPEG
     - 4
     - Camera output, supports network transmission and decode consumption
     - Solution 1: Single output; Solution 2: Dual output
   * - YUV
     - 3
     - DVP camera hardware output, supports display consumption
     - Solution 2: DVP dual output
   * - H264
     - 6
     - Optional configuration (if H264 feature needed)
     - Extended feature

**Important Notes**:

**Solution 1 (Single Output)**:
  - ✅ MJPEG Frame Queue: Camera output, direct network transmission
  - ❌ YUV data: Video pipeline handles internally (no YUV Frame Queue)

**Solution 2 (Dual Output)**:
  - ✅ MJPEG Frame Queue: DVP hardware directly outputs MJPEG
  - ✅ YUV Frame Queue: DVP hardware directly outputs YUV
  - 📌 Key advantage: DVP hardware processes once internally, outputs two streams simultaneously

#### 4.5.2 Performance Optimization
  - **Zero-copy**: Supports multi-consumer sharing of same frame data
  - **Automatic reuse**: frame buffer automatically recycled and reused
  - **Queue management**: Separate free queue and ready queue
  - **Timeout mechanism**: Supports blocking and non-blocking get

#### 4.5.3 Typical Performance Metrics

**Solution 1 (Single Output) Performance**:
  - **MJPEG network transmission**: 864x480@30fps, bandwidth 2-4Mbps
  - **MJPEG decode**: Hardware decode latency <33ms
  - **End-to-end latency**: <200ms (under normal WiFi conditions)
  - **CPU usage**: Medium (MJPEG decode in video pipeline for display)

**Solution 2 (Dual Output) Performance**:
  - **MJPEG network transmission**: 864x480@15fps, bandwidth 1-2Mbps
  - **YUV display**: Hardware direct output, zero latency
  - **End-to-end latency**: <150ms (under normal WiFi conditions, optimal)
  - **CPU usage**: Very low (only YUV processing)

**Network Transmission**:
  - Supports 2Mbps~4Mbps bitrate (MJPEG)
  - Supports CS2/TCP/UDP multiple protocols
  - Good compatibility, no need for H264 decoder


## 5 API Reference

This section provides API interface descriptions for core functions in the project, which implement advanced function calls by encapsulating SDK.

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
 *        - format: Image format (0:MJPEG)
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
 *          - DVP: dvp_camera_turn_on
 *            * Single output mode: Output MJPEG to MJPEG Frame Queue
 *            * Dual output mode: Simultaneously output MJPEG and YUV to respective Frame Queues
 *          - UVC: uvc_camera_turn_on
 *            * Output MJPEG to MJPEG Frame Queue
 *       3. Configure image rotation processing (if display controller is initialized)
 *          - ROTATE: bk_video_pipeline_open_rotate
 *       4. **Key Feature**: This project uses MJPEG format for video transmission, no need for H264 encoding
 * 
 * @note Memory management methods:
 *       - Solution 1 (single output): MJPEG uses queue, decoded YUV handled by video pipeline internally
 *       - Solution 2 (dual output): Both MJPEG and YUV use Frame Queue management
 * 
 * @note Difference from doorbell project:
 *       - doorviewer: Transmits MJPEG, no need for H264 encoder
 *       - doorbell: Transmits H264, needs H264 encoder
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
 *       1. Call corresponding turn-off functions based on camera type
 *          - UVC: uvc_camera_turn_off()
 *            * Disconnect UVC device
 *            * Release MJPEG decoder resources (if used for display)
 *          - DVP: dvp_camera_turn_off()
 *            * Stop DVP hardware output (MJPEG or MJPEG+YUV)
 *            * Turn off camera power
 *       2. Release camera-related resources, including:
 *          - Turn off camera hardware
 *          - Delete camera controller
 *          - Unregister flash operation notifications
 * 
 * @note Difference from doorbell project:
 *       - doorviewer: No need to close H264 encoder (not using H264)
 *       - doorbell: Needs to close H264 encoder
 * 
 * @warning Before calling this function, ensure the camera is properly turned on, otherwise resource leaks may occur
 * @see doorbell_camera_turn_on()
 */
int doorbell_camera_turn_off(void);
```

### 5.2 Video Transmission API

#### 5.2.1 doorbell_video_transfer_turn_on
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
 *        - **Key Feature**: Transmission format is MJPEG
 *        - Get data from MJPEG Frame Queue
 *        - Transmit via CS2/TCP/UDP protocols
 * 
 * @note Data source:
 *        - Solution 1: Get MJPEG data directly output by camera from MJPEG Frame Queue
 *        - Solution 2: Get MJPEG data output by DVP hardware from MJPEG Frame Queue
 * 
 * @note Difference from doorbell project:
 *        - doorviewer: Transmits MJPEG format, bandwidth 2-4Mbps, good compatibility
 *        - doorbell: Transmits H264 format, bandwidth 0.5-2Mbps, high compression rate
 * 
 * @warning Before calling this function, ensure the camera is properly turned on
 * 
 * @see doorbell_video_transfer_turn_off()
 */
int doorbell_video_transfer_turn_on(void);
```

#### 5.2.2 doorbell_video_transfer_turn_off
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
 *        - Release MJPEG transmission related resources
 * 
 * @warning Before calling this function, ensure the video transmission function is properly turned on
 * 
 * @see doorbell_video_transfer_turn_on()
 */
int doorbell_video_transfer_turn_off(void);
```

### 5.3 Display API

#### 5.3.1 doorbell_display_turn_on
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
 * @note Data source and memory management:
 *       - Solution 1 (single output):
 *         * Video pipeline gets data from MJPEG Frame Queue
 *         * Decodes YUV internally (no YUV Frame Queue)
 *         * Video pipeline handles decoded YUV data
 *       - Solution 2 (dual output):
 *         * Get YUV frames output by DVP hardware from YUV Frame Queue
 *         * No MJPEG decoding, directly uses YUV data
 *         * Release via frame_queue_free after display
 * 
 * @warning If device initialization fails, will clean up allocated resources
 * @see doorbell_display_turn_off()
 */
int doorbell_display_turn_on(display_parameters_t *parameters);
```

#### 5.3.2 doorbell_display_turn_off
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
 * @note Resource release:
 *       - Solution 1 (single output): Release video pipeline and MJPEG decoder resources
 *       - Solution 2 (dual output): No need to release decoder resources (not used)
 * 
 * @warning This function will release all display-related resources
 * @see doorbell_display_turn_on()
 */
int doorbell_display_turn_off(void);
```

### 5.4 Audio API

#### 5.4.1 doorbell_audio_turn_on
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
 *        - Initialize audio encoder/decoder (default G711)
 *        - Initialize audio read and write handles
 *        - Start audio processing flow
 * 
 * @warning Before calling this function, ensure audio parameters are correctly configured
 * 
 * @see doorbell_audio_turn_off()
 */
int doorbell_audio_turn_on(audio_parameters_t *parameters);
```

#### 5.4.2 doorbell_audio_turn_off
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

### 5.5 Frame Buffer Queue Management API

This project uses standard version frame queue management, supporting MJPEG, YUV and H264 format frame buffers. Main features:
- **Dual queue structure**: Free queue (free list) and ready queue (ready list)
- **Automatic reuse**: frame buffer automatically recycled and reused
- **Blocking and non-blocking**: Supports blocking and non-blocking get
- **Multi-format support**: Supports MJPEG/YUV/H264 and other formats

#### 5.5.1 frame_queue_init_all
```c
/**
 * @brief Initialize frame queue data structures for all image formats
 * 
 * @return bk_err_t Initialization result
 *         - BK_OK: All queues initialized successfully
 *         - BK_FAIL: Any queue initialization failed
 * 
 * @note This function is responsible for initializing frame queues for all supported image formats, main functions include:
 *        - Initialize MJPEG format frame queue (4 frame buffers)
 *        - Initialize YUV format frame queue (3 frame buffers)
 *        - Initialize H264 format frame queue (6 frame buffers, optional)
 *        - Create free queue and ready queue
 *        - Check initialization result of each queue
 *        - If any queue initialization fails, return failure overall
 * 
 * @warning Before calling this function, ensure system resources are sufficient
 * 
 * @see frame_queue_deinit_all()
 */
bk_err_t frame_queue_init_all(void);
```

#### 5.5.2 frame_queue_deinit_all
```c
/**
 * @brief Release frame queue data structures for all image formats
 * 
 * @return bk_err_t Release result
 *         - BK_OK: All queues released successfully
 * 
 * @note This function is responsible for releasing frame queues for all supported image formats, main functions include:
 *        - Release MJPEG format frame queue
 *        - Release YUV format frame queue
 *        - Release H264 format frame queue (if used)
 *        - Clean up frame buffer resources in all queues
 *        - Deinitialize all queue structures
 * 
 * @warning Before calling this function, ensure all queues are properly initialized
 * 
 * @see frame_queue_init_all()
 */
bk_err_t frame_queue_deinit_all(void);
```

#### 5.5.3 frame_queue_malloc
```c
/**
 * @brief Allocate a frame buffer from the frame queue of specified image format
 * 
 * @param format Image format
 *        - IMAGE_MJPEG: MJPEG format
 *        - IMAGE_YUV: YUV format
 *        - IMAGE_H264: H264 format (optional)
 * 
 * @param size Frame size to allocate (bytes)
 *        - Specify the frame buffer size to allocate
 * 
 * @return frame_buffer_t* Allocation result
 *         - Returns allocated frame buffer pointer on success
 *         - Returns NULL on failure
 * 
 * @note This function is responsible for allocating frame buffer from free queue, main functions include:
 *        - Determine queue index based on image format
 *        - Check if queue is initialized
 *        - Get frame buffer from free queue (free list)
 *        - If existing buffer size is insufficient, reallocate
 *        - Set initial state and attributes of frame
 *        - Return allocated frame buffer pointer
 * 
 * @warning Before calling this function, ensure the queue is properly initialized
 * @warning After allocation, must call frame_queue_complete or cancel operation
 * 
 * @see frame_queue_complete()
 * @see frame_queue_free()
 */
frame_buffer_t *frame_queue_malloc(image_format_t format, uint32_t size);
```

#### 5.5.4 frame_queue_complete
```c
/**
 * @brief Put filled frame into ready queue
 * 
 * @param format Image format
 *        - IMAGE_MJPEG: MJPEG format
 *        - IMAGE_YUV: YUV format
 *        - IMAGE_H264: H264 format (optional)
 * 
 * @param frame Filled frame buffer pointer
 *        - Pointer to frame buffer that needs to be put into ready queue
 * 
 * @return bk_err_t Operation result
 *         - BK_OK: Put successful
 *         - BK_FAIL: Put failed
 * 
 * @note This function puts filled frame into ready queue, main functions include:
 *        - Determine queue index based on image format
 *        - Check if queue is initialized
 *        - Construct frame message structure
 *        - Put frame buffer into ready queue (ready list)
 *        - If put fails, release frame buffer
 * 
 * @warning Before calling this function, ensure the queue is properly initialized
 * @warning This function is usually called by producer after filling data
 * 
 * @see frame_queue_malloc()
 * @see frame_queue_get_frame()
 */
bk_err_t frame_queue_complete(image_format_t format, frame_buffer_t *frame);
```

#### 5.5.5 frame_queue_get_frame
```c
/**
 * @brief Get a frame buffer from ready queue
 * 
 * @param format Image format
 *        - IMAGE_MJPEG: MJPEG format
 *        - IMAGE_YUV: YUV format
 *        - IMAGE_H264: H264 format (optional)
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
 * @note This function gets frame buffer from ready queue, main functions include:
 *        - Determine queue index based on image format
 *        - Check if queue is initialized
 *        - Get frame buffer from ready queue (ready list)
 *        - Support timeout waiting mechanism
 *        - Return obtained frame buffer pointer
 * 
 * @warning Before calling this function, ensure the queue is properly initialized
 * @warning After use, must call frame_queue_free to release
 * 
 * @see frame_queue_complete()
 * @see frame_queue_free()
 */
frame_buffer_t *frame_queue_get_frame(image_format_t format, uint32_t timeout);
```

#### 5.5.6 frame_queue_free
```c
/**
 * @brief Release frame buffer and return it to free queue
 * 
 * @param format Image format
 *        - IMAGE_MJPEG: MJPEG format
 *        - IMAGE_YUV: YUV format
 *        - IMAGE_H264: H264 format (optional)
 * 
 * @param frame Frame buffer pointer to release
 *        - Pointer to frame buffer that needs to be released
 * 
 * @return void
 * 
 * @note This function is responsible for releasing frame buffer and recycling resources, main functions include:
 *        - Determine queue index based on image format
 *        - Check if queue is initialized
 *        - Select appropriate release function based on image format
 *        - Release frame buffer data memory
 *        - Construct message and return to free queue (free list)
 *        - Support release of MJPEG/YUV/H264 and other format frames
 * 
 * @warning Before calling this function, ensure the queue is properly initialized
 * @warning Do not release the same frame repeatedly
 * 
 * @see frame_queue_malloc()
 * @see frame_queue_get_frame()
 */
void frame_queue_free(image_format_t format, frame_buffer_t *frame);
```

### 5.6 Frame Queue Usage Examples

#### 5.6.1 Producer Example (Camera Capture MJPEG)
```c
// 1. Initialize frame queue
frame_queue_init_all();

// 2. Allocate frame buffer
frame_buffer_t *frame = frame_queue_malloc(IMAGE_MJPEG, 100*1024);
if (frame == NULL) {
    // Handle allocation failure
    return;
}

// 3. Fill MJPEG data to frame->frame
// ... camera capture MJPEG data ...

// 4. Put into ready queue after filling successfully
frame_queue_complete(IMAGE_MJPEG, frame);
```

#### 5.6.2 Consumer Example (MJPEG Network Transmission)
```c
// 1. Get frame (blocking wait)
frame_buffer_t *frame = frame_queue_get_frame(IMAGE_MJPEG, RTOS_WAIT_FOREVER);
if (frame) {
    // 2. Use frame data for network transmission
    // ... send MJPEG frame data ...
    
    // 3. Release frame after use
    frame_queue_free(IMAGE_MJPEG, frame);
}
```

#### 5.6.3 Dual Output Solution Example
```c
// DVP hardware simultaneously outputs MJPEG and YUV

// Consumer 1: Network transmission MJPEG
frame_buffer_t *mjpeg_frame = frame_queue_get_frame(IMAGE_MJPEG, 100);
if (mjpeg_frame) {
    // Transmit MJPEG data
    wifi_send(mjpeg_frame->frame, mjpeg_frame->length);
    frame_queue_free(IMAGE_MJPEG, mjpeg_frame);
}

// Consumer 2: LCD display YUV
frame_buffer_t *yuv_frame = frame_queue_get_frame(IMAGE_YUV, 100);
if (yuv_frame) {
    // Display YUV data
    lcd_display(yuv_frame->frame, yuv_frame->width, yuv_frame->height);
    frame_queue_free(IMAGE_YUV, yuv_frame);
}
```

## 6 Important Notes

### 6.1 Hardware Configuration Notes

1. Default uses GPIO_28 to control USB LDO, pull high to power on, pay attention to GPIO conflict issues
2. Default uses GPIO_13 to control LCD LDO, pull high to power on, pay attention to GPIO conflict issues
3. Default uses GPIO_7 to control LCD backlight, pull high to enable, pay attention to GPIO conflict issues

### 6.2 Frame Queue Usage Notes

1. **Must initialize first**: Call `frame_queue_init_all()` to initialize all queues
2. **Proper release**: After use, must call `frame_queue_free()` to release
3. **Avoid repeated release**: Do not call free repeatedly on the same frame
4. **Blocking and non-blocking**: Choose appropriate timeout parameter based on scenario

### 6.3 Memory Management Notes

**Solution 1 (Single Output MJPEG)**:
  - ✅ MJPEG frames: Use MJPEG Frame Queue management
  - ❌ Decoded YUV data: **Video pipeline handles internally** (no YUV Frame Queue)
  - ❌ Video processing pipeline output: Managed internally by pipeline

**Solution 2 (Dual Output MJPEG+YUV)**:
  - ✅ MJPEG frames: Use MJPEG Frame Queue management
  - ✅ YUV frames: Use YUV Frame Queue management
  - 📌 Key advantage: DVP hardware processes once internally, outputs two streams simultaneously

### 6.4 Main Differences from doorbell Project

.. list-table:: Main Differences from doorbell Project
   :header-rows: 1

   * - Feature
     - doorviewer
     - doorbell
   * - Video Transmission Format
     - MJPEG
     - H264
   * - Bandwidth Usage
     - 2-4Mbps
     - 0.5-2Mbps
   * - H264 Encoder
     - Not needed
     - UVC solution needs
   * - Compatibility
     - Better (MJPEG universal)
     - Needs H264 decoder
   * - CPU Usage
     - Low (no encoding)
     - High (software encoding, UVC solution)
   * - Use Case
     - LAN, compatibility priority
     - WAN, bandwidth limited

## 7 System Architecture

The project adopts modular design, mainly including the following modules:

### 7.1 Core Modules

1. **WiFi Module**
   - Responsible for network connection and data transmission
   - Supports STA/AP mode switching
   - Supports CS2/TCP/UDP multiple transmission protocols
   - **Feature**: Directly transmits MJPEG data, no need for H264 encoding

2. **Media Processing Module**
   - Handles image capture (UVC/DVP camera)
   - MJPEG decoding (for LCD display via video pipeline)
   - Frame buffer queue management
   - **Feature**: Uses MJPEG format for video transmission, low CPU usage

3. **Display Module**
   - Manages LCD display
   - Video processing pipeline (rotation, scaling, format conversion)
   - Supports RGB/MCU screens

4. **Bluetooth Module**
   - Provides Bluetooth communication functionality
   - WiFi network configuration functionality
   - A2DP/HFP audio functionality

5. **Audio Module**
   - Audio capture and playback
   - Audio codec (default G711)
   - AEC echo cancellation

### 7.2 Data Flow Architecture

```
Camera Capture → MJPEG Frame Queue → Dual Consumption
                                   ├─ Network Transmission (MJPEG)
                                   └─ Local Display (decode to YUV)
```

Each module interacts through clear API interfaces, ensuring system maintainability and scalability.

## 8 Configuration Instructions

Main configuration options of the project are located in Kconfig files, specific functions can be enabled or disabled by modifying configurations:

### 8.1 Main Configuration Items

- **Camera Type**: UVC or DVP
- **Image Resolution**: Default 864x480
- **Video Transmission Format**: MJPEG (fixed)
- **LCD Screen Type**: RGB/MCU
- **Audio Codec**: G711/G726/OPUS, etc.

## 9 Troubleshooting

### 9.1 Common Issues and Solutions

#### 9.1.1 Camera Cannot Be Recognized

- Check if UVC camera connection is correct and USB interface is not loose
- Ensure camera power supply is normal and check if USB LDO (GPIO_28) is pulled high
- Confirm if camera driver is correctly loaded, check initialization process through logs
- Try replacing with a compatible UVC camera module

#### 9.1.2 Display Abnormalities

- Check if LCD connection is correct and the cable is properly inserted
- Confirm if LCD LDO (GPIO_13) and backlight (GPIO_7) are working normally
- Check if LCD model matches the configuration
- Solution 1: Check if MJPEG decoder in video pipeline is working properly
- Solution 2: Check if YUV frame queue is normal

#### 9.1.3 WiFi Connection Failure

- Confirm WiFi name and password are entered correctly
- Ensure using 2.4G WiFi network (5G WiFi is not supported)
- Check if the distance between device and router is too far
- Try reconnecting through Bluetooth network configuration function

#### 9.1.4 Video Transmission Lag

- Check network connection quality to ensure sufficient bandwidth (MJPEG needs 2-4Mbps)
- Try reducing video resolution and frame rate settings
- Check if there are any abnormalities in frame buffer management
- Use logs to check MJPEG frame queue status
- **Note**: MJPEG format has larger bandwidth usage, ensure WiFi signal strength is sufficient

#### 9.1.5 Memory Insufficient

- Check if frame buffer configuration is reasonable
- Ensure proper release of used frames (call frame_queue_free)
- Avoid releasing the same frame repeatedly
- Check if there are memory leaks

## 10 Performance Optimization Suggestions

### 10.1 Network Transmission Optimization

- Adjust image resolution and frame rate based on network bandwidth
- Prioritize using 2.4G WiFi below 5G (better signal coverage)
- Consider using CS2 protocol to improve transmission efficiency

### 10.2 Display Performance Optimization

- Solution 2 (dual output) has optimal performance, no MJPEG decoding needed
- Use hardware rotation instead of software rotation
- Reasonably configure video processing pipeline parameters

### 10.3 Memory Optimization

- Reasonably configure frame buffer count (MJPEG: 4, YUV: 3)
- Release unused frames in time
- Monitor frame queue status to avoid backlog

## 11 Summary

Core features of doorviewer project:
- ✅ **MJPEG Video Transmission**: Good compatibility, no need for H264 codec
- ✅ **Low CPU Usage**: No need for software H264 encoding
- ✅ **Dual Output Support**: DVP hardware simultaneously outputs MJPEG and YUV
- ⚠️ **Larger Bandwidth Usage**: Suitable for LAN scenarios
- ⚠️ **Lower Compression Rate**: Uses more bandwidth compared to H264

**Selection Recommendations**:
- **LAN Applications**: Choose doorviewer (MJPEG), good compatibility
- **WAN Applications**: Choose doorbell (H264), smaller bandwidth usage
