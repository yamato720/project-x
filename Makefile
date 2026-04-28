SHELL := /bin/bash

TARGET ?= sw_emu
DEVICE ?= xilinx_u55c_gen3x16_xdma_3_202210_1
XPLATFORM ?= /opt/xilinx/platforms/$(DEVICE)/$(DEVICE).xpfm
XILINX_XRT ?= /opt/xilinx/xrt

VPP ?= v++
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

KERNEL_SRC := $(PROJECT_ROOT)/src/krnl_spmv.cpp
HOST_SRC := $(PROJECT_ROOT)/src/host.cpp
CONFIG := $(PROJECT_ROOT)/cfg/u55c.cfg

CXXFLAGS += -std=c++17 -O2 -Wall -Wextra
CXXFLAGS += -I$(XILINX_XRT)/include
LDFLAGS += -L$(XILINX_XRT)/lib -lxrt_coreutil -luuid -pthread -lrt

VPP_FLAGS += -t $(TARGET) --platform $(XPLATFORM) --save-temps --hls.jobs $(HLS_JOBS)
VPP_FLAGS += --temp_dir $(BUILD_DIR)/_x_temp --report_dir $(REPORT_DIR)
VPP_LDFLAGS += --config $(CONFIG)
VPP_LDFLAGS += --vivado.synth.jobs $(VIVADO_JOBS) --vivado.impl.jobs $(VIVADO_JOBS)

.PHONY: help env host xo xclbin build run run-hw tmux-build clean cleanall

help:
	@echo "Project-X U55C SpMV template"
	@echo ""
	@echo "Build host only:"
	@echo "  make host"
	@echo ""
	@echo "Build sw_emu xclbin:"
	@echo "  make build TARGET=sw_emu"
	@echo ""
	@echo "Run sw_emu with parameters:"
	@echo "  make run TARGET=sw_emu ROWS=8 SCALE=2 X0=1"
	@echo ""
	@echo "Build real U55C hardware xclbin:"
	@echo "  make build TARGET=hw"
	@echo ""
	@echo "Run on U55C after hardware build:"
	@echo "  make run TARGET=hw ROWS=8 SCALE=2 X0=1"
	@echo ""
	@echo "Long hardware build in tmux:"
	@echo "  make tmux-build TARGET=hw"

env:
	@test -f "$(XPLATFORM)" || (echo "ERROR: platform not found: $(XPLATFORM)" && exit 1)
	@command -v $(VPP) >/dev/null || (echo "ERROR: v++ not found. Source Vitis 2022.2 first." && exit 1)
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
	emconfigutil --platform $(XPLATFORM) --od $(BUILD_DIR)

run: host $(XCLBIN)
ifeq ($(TARGET),hw)
	$(HOST_EXE) $(XCLBIN) $(ROWS) $(SCALE) $(X0) $(DEVICE_INDEX)
else
	$(MAKE) $(EMCONFIG)
	cd $(BUILD_DIR) && EMCONFIG_PATH=$$PWD XCL_EMULATION_MODE=$(TARGET) ../../host.exe krnl_spmv.xclbin $(ROWS) $(SCALE) $(X0) $(DEVICE_INDEX)
endif

run-hw:
	$(MAKE) run TARGET=hw ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX)

tmux-build:
	@mkdir -p logs
	tmux new -d -s project-x-u55c-build "cd $(PROJECT_ROOT) && make build TARGET=$(TARGET) DEVICE=$(DEVICE) 2>&1 | tee logs/build_$(TARGET)_$$(date +%Y%m%d_%H%M%S).log"
	@echo "tmux session: project-x-u55c-build"
	@echo "attach with:  tmux attach -t project-x-u55c-build"
	@echo "detach with:  Ctrl-b d"

clean:
	rm -rf $(BUILD_ROOT) reports .Xil *.log *.jou *.csv *.run_summary

cleanall: clean
	rm -rf logs
