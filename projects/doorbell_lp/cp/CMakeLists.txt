set(incs .
         db_ipc_msg
         keepalive
         powerctrl
         db_pack
    )

set(srcs
    cp_main.c
    )

list(APPEND srcs
    db_ipc_msg/db_ipc_msg.c
    db_pack/db_pack.c
    keepalive/db_keepalive.c
    powerctrl/powerctrl.c
)

set(prev_req controller_if lwip_intf_v2_1 bt)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${prev_req}")
