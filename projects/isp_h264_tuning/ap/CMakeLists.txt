set(incs .)
set(srcs
    ap_main.c
    media_preview_server.c
    h264e_stream_schema.c
    isp_frame_schema.c
    h264e_stream_project.c
    isp_frame_project.c
)

set(priv_req
    media_service
    multimedia_device_service
    bk_cli
    avdk_utils
    bk_encoder
    bk_camera
    bk_network_transfer
    json
    bk_peripheral
    bk_wifi_driver
    lwip_intf_v2_1
)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")
