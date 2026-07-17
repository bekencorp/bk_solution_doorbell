SDK_DIR = /home/hangxun.zhou/workspace/armino/bk7259_ai_workspace_v1_bk_0707/bk_avdk_smp_release_4.0.1

PROJECT_MAKE_FILE := $(SDK_DIR)/tools/build_tools/build_files/project_main.mk
ifeq ($(wildcard $(PROJECT_MAKE_FILE)),)
    $(error "$(PROJECT_MAKE_FILE) not exist, please check sdk directory.")
endif
include $(PROJECT_MAKE_FILE)
