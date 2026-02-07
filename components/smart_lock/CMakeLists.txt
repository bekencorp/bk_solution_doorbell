set(incs include .)
set(srcs)

if (CONFIG_INTEGRATION_DOORBELL OR CONFIG_INTEGRATION_DOORVIEWER)
	list(APPEND srcs
		src/doorbell_core.c
		src/doorbell_boarding.c
		src/doorbell_network.c
		src/doorbell_network_transfer.c
		src/doorbell_audio_device.c
		src/doorbell_devices.c
		src/doorbell_cmd.c
		src/bk_smart_config.c
		src/decoder/mjpeg_decoder.c
		src/yuv_display/yuv_display.c
	)

	list(APPEND srcs
		src/frame/frame_que_v2.c
	)

if (CONFIG_DVP_CAMERA)
	list(APPEND srcs
		src/camera/dvp.c
	)
endif()

if (CONFIG_USB_CAMERA)
	list(APPEND srcs
		src/camera/uvc.c
	)
endif()

if (CONFIG_INTEGRATION_DOORBELL)
	list(APPEND srcs
		src/camera/doorbell_camera.c
	)
endif()#CONFIG_INTEGRATION_DOORBELL

if (CONFIG_INTEGRATION_DOORVIEWER)
	list(APPEND srcs
		src/camera/doorviewer_camera.c
	)
endif()#CONFIG_INTEGRATION_DOORVIEWER

endif()

set(req multimedia bk_display media_utils bk_network_transfer avdk_utils lwip_intf_v2_1 bk_wifi_driver cs2p2p bt bk_video_pipeline bk_camera)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" REQUIRES "${req}"
)
