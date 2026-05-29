set(incs .)

set(srcs ap_main.c)

set(priv_req multimedia_device_service avdk_utils media_service smart_lock bk_peripheral)
armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")
