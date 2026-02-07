set(incs include  .
        db_ipc_msg
        keepalive
)

set(srcs ap_main.c )

list(APPEND srcs
    db_ipc_msg/db_ipc_msg.c
    keepalive/db_keepalive.c
)

set(priv_req  media_service multimedia smart_lock)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")
