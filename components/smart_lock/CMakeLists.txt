set(incs include .)
set(srcs)

if (CONFIG_SMART_LOCK)

if (CONFIG_INTEGRATION_DOORBELL_KVS)
	list(APPEND srcs
		src/doorbell_devices.c
		src/doorbell_cmd.c
	)
elseif (CONFIG_INTEGRATION_DOORBELL)
	list(APPEND srcs
		src/doorbell_network_transfer.c
		src/doorbell_devices.c
		src/doorbell_cmd.c
		src/bk_smart_config.c
	)
endif()

if (CONFIG_INTEGRATION_DOORBELL AND CONFIG_NTWK_CLIENT_SERVICE_ENABLE)
	list(APPEND srcs
		src/doorbell_keepalive.c
	)
endif()

endif() # CONFIG_SMART_LOCK

set(req doorbell_common media_utils bk_network_transfer avdk_utils lwip_intf_v2_1 bk_wifi_driver cs2p2p bk_bluetooth multimedia_device_service bk_peripheral media_service bk_asr_service bk_vfs)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" REQUIRES "${req}"
)
