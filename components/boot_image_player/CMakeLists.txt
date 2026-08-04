# NOTE: armino_component_register() must be called unconditionally and its
# REQUIRES list must NOT be gated on Kconfig: the build system expands the
# component requirement graph (used for public-include propagation) before the
# per-component sources are chosen. Only the SRCS are gated on the feature.

set(incs include)
set(srcs "")

set(reqs
    bk_decoder
    bk_vfs
    bk_display
    bk_frame_buffer
    media_service
    bk_peripheral
    avdk_utils
    avdk_driver
)

if (CONFIG_BOOT_IMAGE_PLAYER)
    list(APPEND srcs
        src/boot_image_player.c
        src/boot_image_fs.c
        src/boot_image_decoder.c
        src/boot_image_display.c
    )
endif()

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" REQUIRES "${reqs}")
