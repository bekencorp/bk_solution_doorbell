set(incs include)
set(srcs .)
set(priv_req bk_common)

list(APPEND srcs 
	src/mds_img_manager.c
	src/app_camera.c
	src/app_display.c
	src/app_codec.c
	src/app_gpu.c
	src/devices_mgmt.c
	src/devices_cli.c
	src/encode_frame_que.c
	src/app_jpeg_decode.c
	src/app_h264_encode.c
)

if (CONFIG_USB_CAMERA)
list(APPEND srcs src/app_uvc.c)
endif()

if (CONFIG_MDS_SNAPSHOT)
list(APPEND incs bk_codec_util/bk_snapshot/include)
list(APPEND srcs
	bk_codec_util/bk_snapshot/src/bk_snapshot.c
	bk_codec_util/bk_snapshot/src/sw_isp_snapshot.c
)
list(APPEND priv_req bk_encoder bk_camera)
endif()

list(APPEND priv_req unity include avdk_utils avdk_driver bk_peripheral bk_encoder media_service bk_network_transfer)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")

add_definitions(-Wno-unused-variable
				-Wno-unused-but-set-variable
				-Wno-int-to-pointer-cast
				-Wno-pointer-to-int-cast)
