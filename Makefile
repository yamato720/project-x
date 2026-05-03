SHELL := /bin/bash
empty :=
space := $(empty) $(empty)

TARGET ?= sw_emu
DEVICE ?= xilinx_u55c_gen3x16_xdma_3_202210_1
XPLATFORM ?= /opt/xilinx/platforms/$(DEVICE)/$(DEVICE).xpfm
XILINX_XRT ?= /opt/xilinx/xrt
VITIS_ROOT ?= $(strip $(shell \
	if [ -n "$$XILINX_VITIS" ] && [ -x "$$XILINX_VITIS/bin/v++" ]; then \
		printf '%s\n' "$$XILINX_VITIS"; \
	elif [ -x /tools/Xilinx2022/Vitis/2022.2/bin/v++ ]; then \
		printf '%s\n' /tools/Xilinx2022/Vitis/2022.2; \
	elif [ -x /tools/Xilinx/Vitis/2022.2/bin/v++ ]; then \
		printf '%s\n' /tools/Xilinx/Vitis/2022.2; \
	elif [ -d /tools/Xilinx/Vitis ]; then \
		find /tools/Xilinx/Vitis -maxdepth 1 -mindepth 1 -type d | sort -V | tail -n 1; \
	elif command -v v++ >/dev/null 2>&1; then \
		dirname "$$(dirname "$$(command -v v++)")"; \
	fi))
VITIS_VERSION := $(notdir $(VITIS_ROOT))
XILINX_ROOT := $(patsubst %/Vitis/$(VITIS_VERSION),%,$(VITIS_ROOT))
VIVADO_ROOT ?= $(if $(wildcard $(XILINX_ROOT)/Vivado/$(VITIS_VERSION)/bin/vivado),$(XILINX_ROOT)/Vivado/$(VITIS_VERSION))
VITIS_HLS_ROOT ?= $(if $(wildcard $(XILINX_ROOT)/Vitis_HLS/$(VITIS_VERSION)/bin/vitis_hls),$(XILINX_ROOT)/Vitis_HLS/$(VITIS_VERSION))
TOOLCHAIN_BINS := $(strip $(if $(VITIS_ROOT),$(VITIS_ROOT)/bin) $(if $(VITIS_HLS_ROOT),$(VITIS_HLS_ROOT)/bin) $(if $(VIVADO_ROOT),$(VIVADO_ROOT)/bin))

VPP ?= $(if $(VITIS_ROOT),$(VITIS_ROOT)/bin/v++,v++)
EMCONFIGUTIL ?= $(if $(VITIS_ROOT),$(VITIS_ROOT)/bin/emconfigutil,emconfigutil)
CXX ?= g++
HLS_JOBS ?= 2
VIVADO_JOBS ?= 2

ROWS ?= 8
SCALE ?= 2
X0 ?= 1
DEVICE_INDEX ?= 0

PROJECT_ROOT := $(abspath .)
BUILD_ROOT := $(PROJECT_ROOT)/build
BUILD_DIR := $(BUILD_ROOT)/$(TARGET)/$(DEVICE)
REPORT_DIR := $(PROJECT_ROOT)/reports/$(TARGET)/$(DEVICE)
HOST_EXE := $(BUILD_ROOT)/host.exe
XO := $(BUILD_DIR)/krnl_spmv.xo
XCLBIN := $(BUILD_DIR)/krnl_spmv.xclbin
EMCONFIG := $(BUILD_DIR)/emconfig.json
CHISEL_DIR := $(PROJECT_ROOT)/hardware/chisel

KERNEL_SRC := $(PROJECT_ROOT)/hardware/krnl_spmv.cpp
HOST_SRC := $(PROJECT_ROOT)/software/host.cpp
CONFIG := $(PROJECT_ROOT)/cfg/u55c.cfg

ifneq ($(strip $(VITIS_ROOT)),)
export XILINX_VITIS := $(VITIS_ROOT)
endif
export XILINX_XRT := $(XILINX_XRT)
ifneq ($(strip $(VIVADO_ROOT)),)
export XILINX_VIVADO := $(VIVADO_ROOT)
endif
ifneq ($(strip $(VITIS_HLS_ROOT)),)
export XILINX_HLS := $(VITIS_HLS_ROOT)
endif
ifneq ($(strip $(TOOLCHAIN_BINS)),)
export PATH := $(subst $(space),:,$(TOOLCHAIN_BINS)):$(PATH)
endif

CXXFLAGS += -std=c++17 -O2 -Wall -Wextra
CXXFLAGS += -I$(XILINX_XRT)/include
LDFLAGS += -L$(XILINX_XRT)/lib -lxrt_coreutil -luuid -pthread -lrt
LDFLAGS += -Wl,-rpath,$(XILINX_XRT)/lib

VPP_FLAGS += -t $(TARGET) --platform $(XPLATFORM) --save-temps --hls.jobs $(HLS_JOBS)
VPP_FLAGS += --temp_dir $(BUILD_DIR)/_x_temp --report_dir $(REPORT_DIR)
VPP_LDFLAGS += --config $(CONFIG)
VPP_LDFLAGS += --vivado.synth.jobs $(VIVADO_JOBS) --vivado.impl.jobs $(VIVADO_JOBS)

.PHONY: help env host xo xclbin build run run-sw run-hw check tmux-build chisel clean cleanall

help:
	@echo "Project-X U55C SpMV template"
	@echo ""
	@echo "Build host only:"
	@echo "  make host"
	@echo ""
	@echo "Build sw_emu xclbin:"
	@echo "  make build TARGET=sw_emu"
	@echo ""
	@echo "One-command software emulation:"
	@echo "  make run-sw ROWS=8 SCALE=2 X0=1"
	@echo ""
	@echo "Build real U55C hardware xclbin:"
	@echo "  make build TARGET=hw"
	@echo ""
	@echo "One-command hardware test:"
	@echo "  make run-hw ROWS=8 SCALE=2 X0=1"
	@echo ""
	@echo "Run sw_emu and hardware test in sequence:"
	@echo "  make check ROWS=8 SCALE=2 X0=1"
	@echo ""
	@echo "Long hardware build in tmux:"
	@echo "  make tmux-build TARGET=hw"
	@echo ""
	@echo "Generate Chisel Verilog:"
	@echo "  make chisel"

env:
	@test -f "$(XPLATFORM)" || (echo "ERROR: platform not found: $(XPLATFORM)" && exit 1)
	@test -x "$(VPP)" || command -v $(VPP) >/dev/null || (echo "ERROR: v++ not found. Set VITIS_ROOT or VPP to a compatible Vitis install." && exit 1)
	@test -d "$(XILINX_XRT)" || (echo "ERROR: XILINX_XRT not found: $(XILINX_XRT)" && exit 1)

host: $(HOST_EXE)

$(HOST_EXE): $(HOST_SRC)
	@mkdir -p $(BUILD_ROOT)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

xo: $(XO)

$(XO): $(KERNEL_SRC) | env
	@mkdir -p $(BUILD_DIR) $(REPORT_DIR)
	$(VPP) -c $(VPP_FLAGS) -k krnl_spmv -o $@ $<

xclbin build: $(XCLBIN)

$(XCLBIN): $(XO) $(CONFIG) | env
	@mkdir -p $(BUILD_DIR) $(REPORT_DIR)
	$(VPP) -l $(VPP_FLAGS) $(VPP_LDFLAGS) -o $@ $(XO)

$(EMCONFIG): | env
	@mkdir -p $(BUILD_DIR)
	$(EMCONFIGUTIL) --platform $(XPLATFORM) --od $(BUILD_DIR)

run: host $(XCLBIN)
ifeq ($(TARGET),hw)
	$(HOST_EXE) $(XCLBIN) $(ROWS) $(SCALE) $(X0) $(DEVICE_INDEX)
else
	$(MAKE) $(EMCONFIG)
	cd $(BUILD_DIR) && EMCONFIG_PATH=$$PWD XCL_EMULATION_MODE=$(TARGET) ../../host.exe krnl_spmv.xclbin $(ROWS) $(SCALE) $(X0) $(DEVICE_INDEX)
endif

run-sw:
	$(MAKE) run TARGET=sw_emu ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX)

run-hw:
	$(MAKE) run TARGET=hw ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX)

check: run-sw run-hw

tmux-build:
	@mkdir -p logs
	tmux new -d -s project-x-u55c-build "cd $(PROJECT_ROOT) && make build TARGET=$(TARGET) DEVICE=$(DEVICE) 2>&1 | tee logs/build_$(TARGET)_$$(date +%Y%m%d_%H%M%S).log"
	@echo "tmux session: project-x-u55c-build"
	@echo "attach with:  tmux attach -t project-x-u55c-build"
	@echo "detach with:  Ctrl-b d"

chisel:
	cd $(CHISEL_DIR) && sbt "runMain projectx.GenerateAll"

clean:
	rm -rf $(BUILD_ROOT) reports .Xil *.log *.jou *.csv *.run_summary

cleanall: clean
	rm -rf logs $(CHISEL_DIR)/generated $(CHISEL_DIR)/target $(CHISEL_DIR)/project/target
