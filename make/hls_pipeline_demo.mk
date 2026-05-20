SHELL := /bin/bash
empty :=
space := $(empty) $(empty)

# 这个 make 片段只服务 HLS pipeline demo，不参与原有 SpMV/hybrid/chisel_core
# 多变体流程。根目录的 Makefile.hls_pipeline_demo.mk 只负责 include 它。
TARGET ?= sw_emu
DEVICE ?= xilinx_u55c_gen3x16_xdma_3_202210_1
XPLATFORM ?= /opt/xilinx/platforms/$(DEVICE)/$(DEVICE).xpfm
XILINX_XRT ?= /opt/xilinx/xrt
# 自动探测 Vitis 2022.2；也允许外部用 VITIS_ROOT/VPP 覆盖。
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

# lastword(MAKEFILE_LIST) 指向当前片段，所以从 make/ 回到仓库根。
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
BUILD_ROOT := $(PROJECT_ROOT)/build/hls_pipeline_demo
BUILD_DIR := $(BUILD_ROOT)/$(TARGET)/$(DEVICE)
REPORT_DIR := $(PROJECT_ROOT)/reports/hls_pipeline_demo/$(TARGET)/$(DEVICE)
LOG_DIR := $(PROJECT_ROOT)/logs/vpp/hls_pipeline_demo/$(TARGET)/$(DEVICE)

SINGLE_KERNEL_NAME := krnl_hls_pipeline_demo
TOP_PIPELINE_KERNEL_NAME := krnl_hls_top_pipeline_demo
STREAM_SOURCE_KERNEL_NAME := krnl_hls_pipeline_source
STREAM_COMPUTE_KERNEL_NAME := krnl_hls_pipeline_compute
STREAM_SINK_KERNEL_NAME := krnl_hls_pipeline_sink
KERNEL_HEADER := $(PROJECT_ROOT)/hardware/hls_pipeline_demo.hpp
STREAM_KERNEL_HEADER := $(PROJECT_ROOT)/hardware/hls_pipeline_stream_common.hpp
SINGLE_KERNEL_SRC := $(PROJECT_ROOT)/hardware/hls/hls_pipeline_demo/krnl_hls_pipeline_demo.cpp
TOP_PIPELINE_KERNEL_SRC := $(PROJECT_ROOT)/hardware/hls/hls_pipeline_demo/krnl_hls_top_pipeline_demo.cpp
STREAM_SOURCE_KERNEL_SRC := $(PROJECT_ROOT)/hardware/hls/hls_pipeline_demo/krnl_hls_pipeline_source.cpp
STREAM_COMPUTE_KERNEL_SRC := $(PROJECT_ROOT)/hardware/hls/hls_pipeline_demo/krnl_hls_pipeline_compute.cpp
STREAM_SINK_KERNEL_SRC := $(PROJECT_ROOT)/hardware/hls/hls_pipeline_demo/krnl_hls_pipeline_sink.cpp
HOST_SRC := $(PROJECT_ROOT)/software/hls_pipeline_demo_host.cpp
HOST_EXE := $(BUILD_ROOT)/host.exe
SINGLE_XO := $(BUILD_DIR)/$(SINGLE_KERNEL_NAME).xo
TOP_PIPELINE_XO := $(BUILD_DIR)/$(TOP_PIPELINE_KERNEL_NAME).xo
STREAM_SOURCE_XO := $(BUILD_DIR)/$(STREAM_SOURCE_KERNEL_NAME).xo
STREAM_COMPUTE_XO := $(BUILD_DIR)/$(STREAM_COMPUTE_KERNEL_NAME).xo
STREAM_SINK_XO := $(BUILD_DIR)/$(STREAM_SINK_KERNEL_NAME).xo
XOS := $(SINGLE_XO) $(TOP_PIPELINE_XO) $(STREAM_SOURCE_XO) $(STREAM_COMPUTE_XO) $(STREAM_SINK_XO)
XCLBIN := $(BUILD_DIR)/hls_pipeline_demo.xclbin
BITSTREAM_XCLBIN ?= $(BUILD_ROOT)/hw/$(DEVICE)/hls_pipeline_demo.xclbin
EMCONFIG := $(BUILD_DIR)/emconfig.json
CONFIG := $(PROJECT_ROOT)/cfg/hls_pipeline_demo.cfg

# 工具入口。默认值跟现有根 Makefile 保持一致。
VPP ?= $(if $(VITIS_ROOT),$(VITIS_ROOT)/bin/v++,v++)
EMCONFIGUTIL ?= $(if $(VITIS_ROOT),$(VITIS_ROOT)/bin/emconfigutil,emconfigutil)
CXX ?= g++
HLS_JOBS ?= 2
VIVADO_JOBS ?= 2
DEVICE_INDEX ?= 0
ITEMS ?= 32
HOST_ARGS ?=

# 把工具链路径导出给 v++/emconfigutil 及其子进程。
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
CXXFLAGS += -I$(PROJECT_ROOT)/hardware
CXXFLAGS += -I$(XILINX_XRT)/include
LDFLAGS += -L$(XILINX_XRT)/lib -lxrt_coreutil -luuid -pthread -lrt
LDFLAGS += -Wl,-rpath,$(XILINX_XRT)/lib

VPP_FLAGS += -t $(TARGET) --platform $(XPLATFORM) --save-temps --hls.jobs $(HLS_JOBS)
VPP_FLAGS += --temp_dir $(BUILD_DIR)/_x_temp --report_dir $(REPORT_DIR) --log_dir $(LOG_DIR)
VPP_FLAGS += --remote_ip_cache $(PROJECT_ROOT)/.ipcache
VPP_FLAGS += -I$(PROJECT_ROOT)/hardware
# hls_pipeline_demo.cfg 负责声明五个 kernel 实例，并把三 kernel 版本的
# AXI4-Stream 端口连成 source -> compute -> sink。
VPP_LDFLAGS += --config $(CONFIG)
VPP_LDFLAGS += --vivado.synth.jobs $(VIVADO_JOBS) --vivado.impl.jobs $(VIVADO_JOBS)

.PHONY: help env host xo xclbin build build-bitstream bitstream run run-existing run-bitstream clean cleanall

help:
	@echo "Project-X HLS pipeline demo"
	@echo ""
	@echo "Build host:"
	@echo "  make -f Makefile.hls_pipeline_demo.mk host"
	@echo ""
	@echo "Build sw_emu xclbin:"
	@echo "  make -f Makefile.hls_pipeline_demo.mk build TARGET=sw_emu"
	@echo ""
	@echo "Build hardware bitstream/xclbin:"
	@echo "  make -f Makefile.hls_pipeline_demo.mk build-bitstream"
	@echo ""
	@echo "Run sw_emu:"
	@echo "  make -f Makefile.hls_pipeline_demo.mk run TARGET=sw_emu ITEMS=32"
	@echo "  make -f Makefile.hls_pipeline_demo.mk run TARGET=sw_emu ITEMS=32 HOST_ARGS=\"--timing --repeat 5\""
	@echo ""
	@echo "Run hardware bitstream/xclbin:"
	@echo "  make -f Makefile.hls_pipeline_demo.mk run-bitstream ITEMS=32 DEVICE_INDEX=0"
	@echo ""
	@echo "Run existing xclbin:"
	@echo "  make -f Makefile.hls_pipeline_demo.mk run-existing XCLBIN_PATH=/path/to/krnl_hls_pipeline_demo.xclbin"

env:
	@test -f "$(XPLATFORM)" || (echo "ERROR: platform not found: $(XPLATFORM)" && exit 1)
	@test -x "$(VPP)" || command -v "$(VPP)" >/dev/null || (echo "ERROR: v++ not found. Set VITIS_ROOT or VPP." && exit 1)
	@test -d "$(XILINX_XRT)" || (echo "ERROR: XILINX_XRT not found: $(XILINX_XRT)" && exit 1)

host: $(HOST_EXE)

$(HOST_EXE): $(HOST_SRC) $(KERNEL_HEADER)
	@mkdir -p $(BUILD_ROOT)
	@# 只编译 demo 专用 host。它不依赖原有 SpMV host。
	$(CXX) $(CXXFLAGS) -o $@ $(HOST_SRC) $(LDFLAGS)

xo: $(XOS)

$(SINGLE_XO): $(SINGLE_KERNEL_SRC) $(KERNEL_HEADER) | env
	@mkdir -p $(BUILD_DIR) $(REPORT_DIR) $(LOG_DIR)
	@# v++ -c 只把 HLS C++ kernel 编译成 xo，不做平台 link。
	cd $(LOG_DIR) && $(VPP) -c $(VPP_FLAGS) -k $(SINGLE_KERNEL_NAME) -o $@ $<

$(TOP_PIPELINE_XO): $(TOP_PIPELINE_KERNEL_SRC) $(KERNEL_HEADER) | env
	@mkdir -p $(BUILD_DIR) $(REPORT_DIR) $(LOG_DIR)
	cd $(LOG_DIR) && $(VPP) -c $(VPP_FLAGS) -k $(TOP_PIPELINE_KERNEL_NAME) -o $@ $<

$(STREAM_SOURCE_XO): $(STREAM_SOURCE_KERNEL_SRC) $(KERNEL_HEADER) $(STREAM_KERNEL_HEADER) | env
	@mkdir -p $(BUILD_DIR) $(REPORT_DIR) $(LOG_DIR)
	cd $(LOG_DIR) && $(VPP) -c $(VPP_FLAGS) -k $(STREAM_SOURCE_KERNEL_NAME) -o $@ $<

$(STREAM_COMPUTE_XO): $(STREAM_COMPUTE_KERNEL_SRC) $(KERNEL_HEADER) $(STREAM_KERNEL_HEADER) | env
	@mkdir -p $(BUILD_DIR) $(REPORT_DIR) $(LOG_DIR)
	cd $(LOG_DIR) && $(VPP) -c $(VPP_FLAGS) -k $(STREAM_COMPUTE_KERNEL_NAME) -o $@ $<

$(STREAM_SINK_XO): $(STREAM_SINK_KERNEL_SRC) $(KERNEL_HEADER) $(STREAM_KERNEL_HEADER) | env
	@mkdir -p $(BUILD_DIR) $(REPORT_DIR) $(LOG_DIR)
	cd $(LOG_DIR) && $(VPP) -c $(VPP_FLAGS) -k $(STREAM_SINK_KERNEL_NAME) -o $@ $<

xclbin build: $(XCLBIN)

$(XCLBIN): $(XOS) $(CONFIG) | env
	@mkdir -p $(BUILD_DIR) $(REPORT_DIR) $(LOG_DIR)
	@# v++ -l 把 xo 链接成可由 XRT 加载的 xclbin。
	cd $(LOG_DIR) && $(VPP) -l $(VPP_FLAGS) $(VPP_LDFLAGS) -o $@ $(XOS)

build-bitstream bitstream:
	@# 手动编译真实硬件 bitstream。Vitis 最终产物仍是可由 XRT 加载的 .xclbin。
	$(MAKE) -f $(PROJECT_ROOT)/Makefile.hls_pipeline_demo.mk build TARGET=hw DEVICE=$(DEVICE) HLS_JOBS=$(HLS_JOBS) VIVADO_JOBS=$(VIVADO_JOBS)

$(EMCONFIG): | env
	@mkdir -p $(BUILD_DIR)
	@# sw_emu/hw_emu 运行前需要 emconfig.json 描述目标平台。
	$(EMCONFIGUTIL) --platform $(XPLATFORM) --od $(BUILD_DIR)

run: host $(XCLBIN)
ifeq ($(TARGET),hw)
	@# 真实硬件直接加载绝对路径 xclbin。
	$(HOST_EXE) $(XCLBIN) $(ITEMS) $(DEVICE_INDEX) $(HOST_ARGS)
else
	@# emulation 需要在 emconfig.json 所在目录运行，并设置 XCL_EMULATION_MODE。
	$(MAKE) -f $(PROJECT_ROOT)/Makefile.hls_pipeline_demo.mk $(EMCONFIG)
	cd $(BUILD_DIR) && EMCONFIG_PATH=$$PWD XCL_EMULATION_MODE=$(TARGET) "$(HOST_EXE)" "hls_pipeline_demo.xclbin" $(ITEMS) $(DEVICE_INDEX) $(HOST_ARGS)
endif

XCLBIN_PATH ?= $(XCLBIN)
XCLBIN_RUN := $(abspath $(XCLBIN_PATH))

run-existing: host
	@test -f "$(XCLBIN_RUN)" || (echo "ERROR: existing xclbin not found: $(XCLBIN_RUN)" && exit 1)
ifeq ($(TARGET),hw)
	@# 运行已经存在的真实硬件 xclbin，不触发重新构建。
	$(HOST_EXE) "$(XCLBIN_RUN)" $(ITEMS) $(DEVICE_INDEX) $(HOST_ARGS)
else
	@# 运行已经存在的 emulation xclbin，同时确保 emconfig.json 存在。
	$(MAKE) -f $(PROJECT_ROOT)/Makefile.hls_pipeline_demo.mk $(EMCONFIG)
	cd $(BUILD_DIR) && EMCONFIG_PATH=$$PWD XCL_EMULATION_MODE=$(TARGET) "$(HOST_EXE)" "$(XCLBIN_RUN)" $(ITEMS) $(DEVICE_INDEX) $(HOST_ARGS)
endif

run-bitstream:
	@# 运行 build-bitstream 默认生成的真实硬件 xclbin；也可覆盖 BITSTREAM_XCLBIN=/path/to/file.xclbin。
	$(MAKE) -f $(PROJECT_ROOT)/Makefile.hls_pipeline_demo.mk run-existing TARGET=hw DEVICE=$(DEVICE) XCLBIN_PATH="$(BITSTREAM_XCLBIN)" ITEMS=$(ITEMS) DEVICE_INDEX=$(DEVICE_INDEX) HOST_ARGS="$(HOST_ARGS)"

clean:
	@# 只清理 demo 的 build/report/log，不影响原有 SpMV 产物。
	rm -rf $(BUILD_ROOT) $(PROJECT_ROOT)/reports/hls_pipeline_demo $(PROJECT_ROOT)/logs/vpp/hls_pipeline_demo

cleanall: clean
