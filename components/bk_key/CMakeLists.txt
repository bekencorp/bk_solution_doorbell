set(incs include)
set(srcs "")

if (CONFIG_BK_KEY)
list(APPEND srcs
	src/bk_key.c
	src/bk_key_driver.c
)
endif()

# Only depend on SDK drivers and the kernel. NEVER list doorbell business
# components here: the "which key does what" mapping lives in the product layer.
set(priv_req bk_common key)
if (CONFIG_BK_KEY_ADC)
	list(APPEND priv_req adc_key)
endif()

armino_component_register(
	SRCS "${srcs}"
	INCLUDE_DIRS "${incs}"
	PRIV_REQUIRES "${priv_req}"
)
