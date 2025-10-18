set(incs include  .)
set(srcs)


list(APPEND srcs
	lcd_rgb_st7701sn.c
	lcd_i80_st7796s.c
)


set(priv_req multimedia bk_display media_utils avdk_utils  )

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}"
)
