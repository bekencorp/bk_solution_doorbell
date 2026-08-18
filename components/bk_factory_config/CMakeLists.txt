set(incs .)
set(srcs "")

if (CONFIG_BK_FACTORY_CONFIG)
list(APPEND srcs
	bk_factory_config.c
)
endif()

armino_component_register(
    SRCS "${srcs}"
	INCLUDE_DIRS "${incs}"
	PRIV_REQUIRES bk_common
)
