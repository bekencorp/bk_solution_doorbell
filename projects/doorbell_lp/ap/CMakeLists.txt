set(incs .)

set(srcs ap_main.c )

set(priv_req
    multimedia_device_service
    avdk_utils
    media_service
    smart_lock
    bk_network_transfer
    lwip_intf_v2_1
    bk_wifi_driver
    bk_peripheral
    lcd_device
)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")
