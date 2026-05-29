set(incs include  .
        db_ipc_msg
        keepalive
)

set(srcs ap_main.c )

list(APPEND srcs
    db_ipc_msg/db_ipc_msg.c
    keepalive/db_keepalive.c
)

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
