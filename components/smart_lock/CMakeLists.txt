set(incs include  .)
set(srcs)

if (CONFIG_INTEGRATION_DOORBELL)
	list(APPEND srcs
		src/doorbell_core.c
		src/doorbell_boarding.c
		src/doorbell_sdp.c
		src/doorbell_network.c
		src/doorbell_transmission.c
		src/doorbell_udp_service.c
		src/doorbell_tcp_service.c
		src/doorbell_devices.c
		src/doorbell_audio_device.c
		src/doorbell_video_device.c
		src/doorbell_cmd.c
		src/bk_smart_config.c
		src/doorbell_frame_que.c
	)
endif()

if (CONFIG_INTEGRATION_DOORBELL_CS2)
list(APPEND srcs
	src/doorbell_cs2_service.c
)
endif()

set(priv_req multimedia bk_display media_utils wifi_transfer avdk_utils lwip_intf_v2_1 bk_wifi_driver cs2p2p bt bk_video_pipeline bk_camera)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}"
)
