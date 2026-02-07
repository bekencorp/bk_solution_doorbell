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
		src/camera/uvc.c
		src/camera/dvp.c
		src/decoder/mjpeg_decoder.c
		src/yuv_display/yuv_display.c
	)

	# V1版本：多消费者帧队列管理（可选）
	# 取消下面的注释来启用V1版本
	# list(APPEND srcs
	# 	src/frame/frame_que.c
	# )

	# V2版本：多消费者帧队列管理（可选）
	# 取消下面的注释来启用V2版本
	list(APPEND srcs
		src/frame/frame_que_v2.c
	)

	# V2测试代码（可选，仅用于开发和测试）
	# 取消下面的注释来启用测试
	# list(APPEND srcs
	# 	src/frame/frame_que_v2_test.c
	# )

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

set(priv_req multimedia bk_display media_utils bk_network_transfer avdk_utils lwip_intf_v2_1 bk_wifi_driver cs2p2p bt bk_video_pipeline bk_camera)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}"
)
