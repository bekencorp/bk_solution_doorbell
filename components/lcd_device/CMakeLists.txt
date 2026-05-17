set(incs  .)


list(APPEND srcs
)

set(priv_req bk_display media_utils avdk_utils)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}"
)
