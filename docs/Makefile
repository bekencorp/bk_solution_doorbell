


export ARMINO_SOC := bk7259
export CMD_TARGET := $(MAKECMDGOALS)


ifeq ("$(ARMINO_SOC)", "")
	ARMINO_SOC := bk7259
	ARMINO_TARGET := $(MAKECMDGOALS)
else
	ARMINO_TARGET := build
endif

export ARMINO_SOC_NAME := $(ARMINO_SOC)

# verify enable multithread build.
ifdef BK_JENKINS_ID
    MAKEFLAGS += -j2
endif

ifndef PRINT_SUMMARY
	PRINT_SUMMARY := 1
endif

.PHONY: all

help:
	@echo ""
	@echo " make clean - clean build"
	@echo " make doc - generate doc"
	@echo ""

common:
	@echo "ARMINO_SOC is set to $(ARMINO_SOC)"
	@echo "ARMINO_TARGET is set to $(ARMINO_TARGET)"

all: doc


DOCS_PARAMTERS:=
ifneq ("$(DOCS_TARGET)", "")
	DOCS_PARAMTERS += --target $(DOCS_TARGET)
endif
ifneq ("$(DOCS_TYPE)", "")
	DOCS_PARAMTERS += --type $(DOCS_TYPE)
endif
ifneq ("$(DOCS_VERSION)", "")
	DOCS_PARAMTERS += --version $(DOCS_VERSION)
endif


doc:
	@python3 ./armino_doc.py --target $(ARMINO_SOC) $(DOCS_PARAMTERS)


clean:
	@python3 ./armino_doc.py --clean True
	@rm -rf ./build


