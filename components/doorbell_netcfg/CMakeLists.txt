set(incs include)
set(srcs "")

if (CONFIG_DOORBELL_NETCFG)
list(APPEND srcs
	src/doorbell_netcfg.c
)
endif()

armino_component_register(
	SRCS "${srcs}"
	INCLUDE_DIRS "${incs}"
	PRIV_REQUIRES bk_common bk_event bk_netif bk_wifi_driver easy_flash
	              bk_network_provisioning bk_bluetooth doorbell_common
	              bk_network_transfer cs2p2p
)
