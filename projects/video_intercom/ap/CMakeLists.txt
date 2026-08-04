set(incs .)

set(srcs ap_main.c )

if (CONFIG_AUD_PARAM_CTRL)
    list(APPEND incs ./audio_param)
    list(APPEND srcs audio_param/audio_para.c)
endif()

set(priv_req
    multimedia_device_service
    avdk_utils
    media_service
    smart_intercom
    bk_network_transfer
    lwip_intf_v2_1
    bk_wifi_driver
    bk_peripheral
    lcd_device
    boot_video_player
    boot_image_player
)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")
