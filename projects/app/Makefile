ifeq ("$(SDK_DIR)", "")
	SDK_DIR := $(abspath ../..)
else
	SDK_DIR := $(abspath $(SDK_DIR))
endif

PROJECT_MAKE_FILE := $(SDK_DIR)/tools/build_tools/build_files/project_main.mk
ifeq ($(wildcard $(PROJECT_MAKE_FILE)),)
    $(error "$(PROJECT_MAKE_FILE) not exist, please check sdk directory.")
endif
include $(PROJECT_MAKE_FILE)
