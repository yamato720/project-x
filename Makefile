SHELL := /bin/bash
empty :=
space := $(empty) $(empty)

TARGET ?= sw_emu
VARIANT ?= hybrid
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
VIVADO ?= $(if $(VIVADO_ROOT),$(VIVADO_ROOT)/bin/vivado,vivado)
CXX ?= g++
HLS_JOBS ?= 2
VIVADO_JOBS ?= 2
CHISEL_JAVA_HOME ?= $(if $(wildcard /usr/lib/jvm/java-17-openjdk-amd64/bin/java),/usr/lib/jvm/java-17-openjdk-amd64,$(patsubst %/bin/java,%,$(shell readlink -f $$(command -v java) 2>/dev/null)))

ROWS ?= 8
SCALE ?= 2
X0 ?= 1
DEVICE_INDEX ?= 0
TIMING ?= 1
REPEAT ?= 1
WARMUP ?= 0
HOST_ARGS ?=

PROJECT_ROOT := $(abspath .)
BUILD_ROOT := $(PROJECT_ROOT)/build
BUILD_DIR := $(BUILD_ROOT)/$(VARIANT)/$(TARGET)/$(DEVICE)
HW_BUILD_DIR := $(BUILD_ROOT)/$(VARIANT)/hw/$(DEVICE)
REPORT_DIR := $(PROJECT_ROOT)/reports/$(VARIANT)/$(TARGET)/$(DEVICE)
LOG_DIR := $(PROJECT_ROOT)/logs
VPP_LOG_DIR := $(LOG_DIR)/vpp/$(VARIANT)/$(TARGET)/$(DEVICE)
VIVADO_LOG_DIR := $(LOG_DIR)/vivado/$(VARIANT)
CHISEL_VIVADO_LOG_DIR := $(LOG_DIR)/vivado/chisel
HOST_EXE := $(BUILD_ROOT)/$(VARIANT)/host.exe
XO := $(BUILD_DIR)/krnl_spmv.xo
XCLBIN := $(BUILD_DIR)/krnl_spmv.xclbin
XCLBIN_PATH_USER_SET := $(if $(filter undefined,$(origin XCLBIN_PATH)),,1)
XCLBIN_PATH ?= $(XCLBIN)
XCLBIN_RUN := $(abspath $(XCLBIN_PATH))
SW_EMU_XCLBIN := $(BUILD_ROOT)/$(VARIANT)/sw_emu/$(DEVICE)/krnl_spmv.xclbin
HW_XCLBIN := $(BUILD_ROOT)/$(VARIANT)/hw/$(DEVICE)/krnl_spmv.xclbin
RUN_SW_XCLBIN_PATH := $(if $(XCLBIN_PATH_USER_SET),$(XCLBIN_PATH),$(SW_EMU_XCLBIN))
RUN_HW_XCLBIN_PATH := $(if $(XCLBIN_PATH_USER_SET),$(XCLBIN_PATH),$(HW_XCLBIN))
VIVADO_LINK_DIR := $(HW_BUILD_DIR)/_x_temp/link/vivado/vpl
VIVADO_XPR := $(VIVADO_LINK_DIR)/prj/prj.xpr
VIVADO_IMPL_DIR := $(VIVADO_LINK_DIR)/prj/prj.runs/impl_1
VIVADO_ROUTED_DCP := $(VIVADO_IMPL_DIR)/level0_wrapper_routed.dcp
VIVADO_OPEN_ROUTED_TCL := $(BUILD_ROOT)/$(VARIANT)/open_routed_vivado.tcl
VIVADO_SCALE ?= 2
VIVADO_GDK_SCALE ?= 1
VIVADO_GDK_DPI_SCALE ?= 1
VIVADO_GUI_ENV = QT_AUTO_SCREEN_SCALE_FACTOR=0 QT_ENABLE_HIGHDPI_SCALING=0 QT_SCALE_FACTOR="$(VIVADO_SCALE)" GDK_SCALE="$(VIVADO_GDK_SCALE)" GDK_DPI_SCALE="$(VIVADO_GDK_DPI_SCALE)" _JAVA_OPTIONS="-Dsun.java2d.uiScale=$(VIVADO_SCALE) $${_JAVA_OPTIONS:-}"
VIVADO_REPORT_DIR := $(PROJECT_ROOT)/reports/$(VARIANT)/hw/$(DEVICE)
VIVADO_PACKAGE_DIR := $(BUILD_ROOT)/packages/$(VARIANT)
VIVADO_PACKAGE_NAME ?= project-x-vivado-view
VIVADO_PACKAGE_MIN := $(VIVADO_PACKAGE_DIR)/$(VIVADO_PACKAGE_NAME)-$(VARIANT)-min.tar.gz
VIVADO_PACKAGE_FULL := $(VIVADO_PACKAGE_DIR)/$(VIVADO_PACKAGE_NAME)-$(VARIANT)-full.tar.gz
EMCONFIG := $(BUILD_DIR)/emconfig.json
CHISEL_DIR := $(PROJECT_ROOT)/hardware/chisel
CHISEL_OUTER_DIR := $(CHISEL_DIR)/generated/outer
# 外积 tile 的 Chisel/HLS black-box 产物。
# hybrid/chisel_core 内核源文件只调用 C 函数 outer_product_tile_bits；
# 真正硬件实现由这些文件在 v++ -c 前准备好，再通过 HLS pre Tcl
# 注册进 Vitis HLS 工程。
CHISEL_OUTER_V := $(CHISEL_OUTER_DIR)/outer_product_tile_bits.v
CHISEL_OUTER_HPP := $(CHISEL_OUTER_DIR)/outer_product_tile.hpp
CHISEL_OUTER_MODEL := $(CHISEL_OUTER_DIR)/outer_product_tile_model.cpp
CHISEL_OUTER_JSON := $(CHISEL_OUTER_DIR)/outer_product_tile.json
CHISEL_OUTER_IP_TCL := $(CHISEL_OUTER_DIR)/create_outer_product_tile_dmul_ip.tcl
CHISEL_OUTER_IP_V := $(CHISEL_OUTER_DIR)/ip/outer_product_tile_dmul_ip/synth/outer_product_tile_dmul_ip.v
CHISEL_OUTER_IP_RFS := $(CHISEL_OUTER_DIR)/ip/outer_product_tile_dmul_ip/hdl/floating_point_v7_1_rfs.v
CHISEL_SPMV_ROW_DIR := $(CHISEL_DIR)/generated/spmv_row
CHISEL_SPMV_ROW_V := $(CHISEL_SPMV_ROW_DIR)/spmv_row_muladd_bits.v
CHISEL_SPMV_ROW_HPP := $(CHISEL_SPMV_ROW_DIR)/spmv_row_muladd.hpp
CHISEL_SPMV_ROW_MODEL := $(CHISEL_SPMV_ROW_DIR)/spmv_row_muladd_model.cpp
CHISEL_SPMV_ROW_JSON := $(CHISEL_SPMV_ROW_DIR)/spmv_row_muladd.json
CHISEL_SPMV_ROW_IP_TCL := $(CHISEL_SPMV_ROW_DIR)/create_spmv_row_fp_ips.tcl
CHISEL_SPMV_ROW_DMUL_IP_V := $(CHISEL_SPMV_ROW_DIR)/ip/spmv_row_dmul_ip/synth/spmv_row_dmul_ip.v
CHISEL_SPMV_ROW_DADD_IP_V := $(CHISEL_SPMV_ROW_DIR)/ip/spmv_row_dadd_ip/synth/spmv_row_dadd_ip.v
CHISEL_SPMV_ROW_IP_RFS := $(CHISEL_SPMV_ROW_DIR)/ip/spmv_row_dmul_ip/hdl/floating_point_v7_1_rfs.v
CHISEL_STAMP := $(CHISEL_DIR)/generated/.stamp
# HLS pre Tcl 会执行 add_files -blackbox ...json。
# 没有它，v++/HLS 只知道当前被 VARIANT 选中的 kernel C++ 文件，
# 无法把 C 函数调用绑定到 RTL black-box。
HLS_PRE_TCL_HYBRID := $(CHISEL_DIR)/add_outer_product_blackbox.tcl
HLS_PRE_TCL_CHISEL_CORE := $(CHISEL_DIR)/add_chisel_core_blackboxes.tcl

KERNEL_COMMON := $(PROJECT_ROOT)/hardware/krnl_spmv_common.hpp
KERNEL_SRC_HLS := $(PROJECT_ROOT)/hardware/hls/krnl_spmv.cpp
KERNEL_SRC_HYBRID := $(PROJECT_ROOT)/hardware/hybrid/krnl_spmv.cpp
KERNEL_SRC_CHISEL_CORE := $(PROJECT_ROOT)/hardware/chisel_core/krnl_spmv.cpp
HOST_SRC := $(PROJECT_ROOT)/software/host.cpp
CONFIG := $(PROJECT_ROOT)/cfg/u55c.cfg
CHISEL_SRCS := $(shell find $(CHISEL_DIR)/src/main/scala -type f 2>/dev/null)

VALID_VARIANTS := hls hybrid chisel_core
ifeq ($(filter $(VARIANT),$(VALID_VARIANTS)),)
$(error Unknown VARIANT='$(VARIANT)'. Use one of: $(VALID_VARIANTS))
endif

ifeq ($(VARIANT),hls)
KERNEL_SRC := $(KERNEL_SRC_HLS)
BLACKBOX_DEPS :=
BLACKBOX_PRE_TCL :=
else ifeq ($(VARIANT),hybrid)
KERNEL_SRC := $(KERNEL_SRC_HYBRID)
BLACKBOX_DEPS := $(CHISEL_OUTER_V) $(CHISEL_OUTER_HPP) $(CHISEL_OUTER_MODEL) $(CHISEL_OUTER_JSON) $(CHISEL_OUTER_IP_V) $(CHISEL_OUTER_IP_RFS) $(HLS_PRE_TCL_HYBRID)
BLACKBOX_PRE_TCL := --hls.pre_tcl $(HLS_PRE_TCL_HYBRID)
else ifeq ($(VARIANT),chisel_core)
KERNEL_SRC := $(KERNEL_SRC_CHISEL_CORE)
BLACKBOX_DEPS := $(CHISEL_OUTER_V) $(CHISEL_OUTER_HPP) $(CHISEL_OUTER_MODEL) $(CHISEL_OUTER_JSON) $(CHISEL_OUTER_IP_V) $(CHISEL_OUTER_IP_RFS) $(CHISEL_SPMV_ROW_V) $(CHISEL_SPMV_ROW_HPP) $(CHISEL_SPMV_ROW_MODEL) $(CHISEL_SPMV_ROW_JSON) $(CHISEL_SPMV_ROW_DMUL_IP_V) $(CHISEL_SPMV_ROW_DADD_IP_V) $(CHISEL_SPMV_ROW_IP_RFS) $(HLS_PRE_TCL_CHISEL_CORE)
BLACKBOX_PRE_TCL := --hls.pre_tcl $(HLS_PRE_TCL_CHISEL_CORE)
XO_POSTPROCESS := python3 scripts/patch_xo_floating_point.py
endif

XO_POSTPROCESS ?= true

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
VPP_FLAGS += --temp_dir $(BUILD_DIR)/_x_temp --report_dir $(REPORT_DIR) --log_dir $(VPP_LOG_DIR)
VPP_FLAGS += --remote_ip_cache $(PROJECT_ROOT)/.ipcache
VPP_FLAGS += -I$(PROJECT_ROOT)/hardware
VPP_FLAGS += -I$(CHISEL_OUTER_DIR)
# 对 hybrid/chisel_core 变体，把 Chisel/Vivado IP black-box 注册到 HLS 工程。
VPP_FLAGS += $(BLACKBOX_PRE_TCL)
VPP_LDFLAGS += --config $(CONFIG)
VPP_LDFLAGS += --vivado.synth.jobs $(VIVADO_JOBS) --vivado.impl.jobs $(VIVADO_JOBS)

HOST_TIMING_ARGS :=
ifeq ($(TIMING),1)
HOST_TIMING_ARGS += --timing
endif
ifneq ($(REPEAT),1)
HOST_TIMING_ARGS += --repeat $(REPEAT)
endif
ifneq ($(WARMUP),0)
HOST_TIMING_ARGS += --warmup $(WARMUP)
endif
HOST_RUN_ARGS := $(HOST_TIMING_ARGS) $(HOST_ARGS)

.PHONY: help env vivado-env x11-check chisel-env host xo xclbin build build-hls build-hybrid build-chisel-core build-sw-hls build-sw-hybrid build-sw-chisel-core build-hw-hls build-hw-hybrid build-hw-chisel-core build-all-sw build-all-hw build-all-variants run run-existing run-sw run-sw-existing run-hw run-hw-existing run-all-sw run-all-sw-existing run-all-hw run-all-hw-existing run-sw-hls run-sw-hybrid run-sw-chisel-core run-hw-hls run-hw-hybrid run-hw-chisel-core check check-hls check-hybrid check-chisel-core test-all-variants tmux-build chisel vivado-open vivado-project vivado-routed vivado-package vivado-package-min vivado-package-full vivado-package-all clean cleanall

help:
	@echo "Project-X U55C SpMV template"
	@echo "Variants: hls, hybrid, chisel_core"
	@echo ""
	@echo "Build host only:"
	@echo "  make host"
	@echo ""
	@echo "Build sw_emu xclbin:"
	@echo "  make build TARGET=sw_emu VARIANT=hybrid"
	@echo "  make build-sw-chisel-core"
	@echo "  make build-all-sw"
	@echo "  make build-all-variants BUILD_MODE=sw"
	@echo ""
	@echo "One-command software emulation:"
	@echo "  make run-sw VARIANT=hybrid ROWS=8 SCALE=2 X0=1"
	@echo "  make run-all-sw ROWS=8 SCALE=2 X0=1"
	@echo ""
	@echo "Run an existing sw_emu xclbin without rebuilding it:"
	@echo "  make run-sw-existing VARIANT=hybrid ROWS=8 SCALE=2 X0=1"
	@echo "  make run-all-sw-existing ROWS=8 SCALE=2 X0=1"
	@echo ""
	@echo "Build real U55C hardware xclbin:"
	@echo "  make build TARGET=hw VARIANT=hybrid"
	@echo "  make build-hw-chisel-core"
	@echo "  make build-all-hw"
	@echo "  make build-all-variants BUILD_MODE=hw"
	@echo ""
	@echo "One-command hardware test:"
	@echo "  make run-hw VARIANT=hybrid ROWS=8 SCALE=2 X0=1"
	@echo "  make run-all-hw ROWS=8 SCALE=2 X0=1"
	@echo ""
	@echo "Run an existing hardware xclbin without rebuilding it:"
	@echo "  make run-hw-existing VARIANT=hybrid ROWS=8 SCALE=2 X0=1"
	@echo "  make run-all-hw-existing ROWS=8 SCALE=2 X0=1"
	@echo ""
	@echo "Host-side timing:"
	@echo "  make run-hw-existing VARIANT=hybrid TIMING=1 WARMUP=1 REPEAT=5"
	@echo ""
	@echo "Run sw_emu and hardware test in sequence:"
	@echo "  make check ROWS=8 SCALE=2 X0=1"
	@echo "  make check-hybrid ROWS=8 SCALE=2 X0=1"
	@echo ""
	@echo "One-command all-variant tests:"
	@echo "  make test-all-variants TEST_MODE=sw   ROWS=8  SCALE=2 X0=1"
	@echo "  make test-all-variants TEST_MODE=hw   ROWS=8  SCALE=2 X0=1"
	@echo "  make test-all-variants TEST_MODE=both ROWS=8  SCALE=2 X0=1"
	@echo ""
	@echo "Change problem size / scale:"
	@echo "  make run-sw VARIANT=hybrid ROWS=64 SCALE=3 X0=10"
	@echo "  make test-all-variants TEST_MODE=sw ROWS=64 SCALE=3 X0=10"
	@echo ""
	@echo "Long hardware build in tmux:"
	@echo "  make tmux-build TARGET=hw"
	@echo ""
	@echo "Generate Chisel Verilog:"
	@echo "  make chisel"
	@echo "  make chisel CHISEL_JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64"
	@echo ""
	@echo "Open generated Vivado hardware project through X11:"
	@echo "  make vivado-open"
	@echo "  make vivado-open VIVADO_SCALE=1   # default VIVADO_SCALE=2"
	@echo ""
	@echo "Open final routed checkpoint through X11:"
	@echo "  make vivado-routed"
	@echo "  make vivado-routed VIVADO_SCALE=1 # default VIVADO_SCALE=2"
	@echo ""
	@echo "Package Vivado files for local download:"
	@echo "  make vivado-package       # routed DCP + reports"
	@echo "  make vivado-package-full  # full Vivado vpl directory + reports"
	@echo "  make vivado-package-all PACKAGE_MODE=both"

env:
	@test -f "$(XPLATFORM)" || (echo "ERROR: platform not found: $(XPLATFORM)" && exit 1)
	@test -x "$(VPP)" || command -v $(VPP) >/dev/null || (echo "ERROR: v++ not found. Set VITIS_ROOT or VPP to a compatible Vitis install." && exit 1)
	@test -x "$(VIVADO)" || command -v $(VIVADO) >/dev/null || (echo "ERROR: vivado not found. Set VIVADO_ROOT or VIVADO to a compatible Vivado install." && exit 1)
	@test -d "$(XILINX_XRT)" || (echo "ERROR: XILINX_XRT not found: $(XILINX_XRT)" && exit 1)

vivado-env:
	@test -x "$(VIVADO)" || command -v "$(VIVADO)" >/dev/null || (echo "ERROR: vivado not found. Set VIVADO_ROOT or VIVADO to a compatible Vivado install." && exit 1)

x11-check:
	@test -n "$$DISPLAY" || (echo "ERROR: DISPLAY is empty. Reconnect with X11 forwarding, for example: ssh -Y <host>" && exit 1)

chisel-env:
	@test -x "$(CHISEL_JAVA_HOME)/bin/java" || (echo "ERROR: Java not found at CHISEL_JAVA_HOME=$(CHISEL_JAVA_HOME). Install OpenJDK 17+ or set CHISEL_JAVA_HOME." && exit 1)
	@test -x "$(CHISEL_JAVA_HOME)/bin/javac" || (echo "ERROR: javac not found at CHISEL_JAVA_HOME=$(CHISEL_JAVA_HOME). Install a JDK, not just a JRE." && exit 1)
	@JAVA_VERSION="$$("$(CHISEL_JAVA_HOME)/bin/java" -version 2>&1 | awk -F '"' '/version/ {print $$2}')"; \
	JAVA_MAJOR="$$(printf '%s\n' "$$JAVA_VERSION" | awk -F. '{if ($$1 == "1") print $$2; else print $$1}')"; \
	if [ "$$JAVA_MAJOR" -lt 17 ]; then \
		echo "ERROR: Chisel 7.x is configured to use Java $$JAVA_VERSION. Use OpenJDK 17+ via CHISEL_JAVA_HOME."; \
		exit 1; \
	fi; \
	echo "Chisel Java: $$JAVA_VERSION ($(CHISEL_JAVA_HOME))"

host: $(HOST_EXE)

$(HOST_EXE): $(HOST_SRC)
	@mkdir -p $(BUILD_ROOT)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

xo: $(XO)

$(XO): $(KERNEL_SRC) $(KERNEL_COMMON) $(BLACKBOX_DEPS) | env
	@mkdir -p $(BUILD_DIR) $(REPORT_DIR) $(VPP_LOG_DIR)
	cd $(VPP_LOG_DIR) && $(VPP) -c $(VPP_FLAGS) -k krnl_spmv -o $@ $<
	cd $(PROJECT_ROOT) && $(XO_POSTPROCESS) "$@"

xclbin build: $(XCLBIN)

build-hls:
	$(MAKE) build VARIANT=hls TARGET=$(TARGET) DEVICE=$(DEVICE)

build-hybrid:
	$(MAKE) build VARIANT=hybrid TARGET=$(TARGET) DEVICE=$(DEVICE)

build-chisel-core:
	$(MAKE) build VARIANT=chisel_core TARGET=$(TARGET) DEVICE=$(DEVICE)

build-sw-hls:
	$(MAKE) build VARIANT=hls TARGET=sw_emu DEVICE=$(DEVICE)

build-sw-hybrid:
	$(MAKE) build VARIANT=hybrid TARGET=sw_emu DEVICE=$(DEVICE)

build-sw-chisel-core:
	$(MAKE) build VARIANT=chisel_core TARGET=sw_emu DEVICE=$(DEVICE)

build-hw-hls:
	$(MAKE) build VARIANT=hls TARGET=hw DEVICE=$(DEVICE)

build-hw-hybrid:
	$(MAKE) build VARIANT=hybrid TARGET=hw DEVICE=$(DEVICE)

build-hw-chisel-core:
	$(MAKE) build VARIANT=chisel_core TARGET=hw DEVICE=$(DEVICE)

build-logged:
	bash scripts/run-build-with-log.sh "$(TARGET)" "$(VARIANT)" "$(DEVICE)"

build-all-sw:
	$(MAKE) build VARIANT=hls TARGET=sw_emu DEVICE=$(DEVICE)
	$(MAKE) build VARIANT=hybrid TARGET=sw_emu DEVICE=$(DEVICE)
	$(MAKE) build VARIANT=chisel_core TARGET=sw_emu DEVICE=$(DEVICE)

build-all-hw:
	$(MAKE) build VARIANT=hls TARGET=hw DEVICE=$(DEVICE)
	$(MAKE) build VARIANT=hybrid TARGET=hw DEVICE=$(DEVICE)
	$(MAKE) build VARIANT=chisel_core TARGET=hw DEVICE=$(DEVICE)

BUILD_MODE ?= both

build-all-variants:
	bash scripts/build-all-variants.sh "$(BUILD_MODE)"

$(XCLBIN): $(XO) $(CONFIG) | env
	@mkdir -p $(BUILD_DIR) $(REPORT_DIR) $(VPP_LOG_DIR)
	cd $(VPP_LOG_DIR) && $(VPP) -l $(VPP_FLAGS) $(VPP_LDFLAGS) -o $@ $(XO)

$(EMCONFIG): | env
	@mkdir -p $(BUILD_DIR)
	$(EMCONFIGUTIL) --platform $(XPLATFORM) --od $(BUILD_DIR)

run: host $(XCLBIN)
ifeq ($(TARGET),hw)
	$(HOST_EXE) $(XCLBIN) $(ROWS) $(SCALE) $(X0) $(DEVICE_INDEX) $(HOST_RUN_ARGS)
else
	$(MAKE) $(EMCONFIG)
	cd $(BUILD_DIR) && EMCONFIG_PATH=$$PWD XCL_EMULATION_MODE=$(TARGET) "$(HOST_EXE)" krnl_spmv.xclbin $(ROWS) $(SCALE) $(X0) $(DEVICE_INDEX) $(HOST_RUN_ARGS)
endif

run-existing: host
	@test -f "$(XCLBIN_RUN)" || (echo "ERROR: existing xclbin not found: $(XCLBIN_RUN). Build it first or set XCLBIN_PATH=/path/to/file.xclbin." && exit 1)
ifeq ($(TARGET),hw)
	$(HOST_EXE) "$(XCLBIN_RUN)" $(ROWS) $(SCALE) $(X0) $(DEVICE_INDEX) $(HOST_RUN_ARGS)
else
	$(MAKE) $(EMCONFIG)
	cd $(BUILD_DIR) && EMCONFIG_PATH=$$PWD XCL_EMULATION_MODE=$(TARGET) "$(HOST_EXE)" "$(XCLBIN_RUN)" $(ROWS) $(SCALE) $(X0) $(DEVICE_INDEX) $(HOST_RUN_ARGS)
endif

run-sw:
	$(MAKE) run TARGET=sw_emu VARIANT=$(VARIANT) ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"

run-sw-existing:
	$(MAKE) run-existing TARGET=sw_emu VARIANT=$(VARIANT) ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)" XCLBIN_PATH="$(RUN_SW_XCLBIN_PATH)"

run-hw:
	$(MAKE) run TARGET=hw VARIANT=$(VARIANT) ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"

run-hw-existing:
	$(MAKE) run-existing TARGET=hw VARIANT=$(VARIANT) ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)" XCLBIN_PATH="$(RUN_HW_XCLBIN_PATH)"

run-all-sw:
	$(MAKE) run-sw VARIANT=hls ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"
	$(MAKE) run-sw VARIANT=hybrid ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"
	$(MAKE) run-sw VARIANT=chisel_core ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"

run-all-sw-existing:
	$(MAKE) run-sw-existing VARIANT=hls ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"
	$(MAKE) run-sw-existing VARIANT=hybrid ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"
	$(MAKE) run-sw-existing VARIANT=chisel_core ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"

run-all-hw:
	$(MAKE) run-hw VARIANT=hls ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"
	$(MAKE) run-hw VARIANT=hybrid ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"
	$(MAKE) run-hw VARIANT=chisel_core ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"

run-all-hw-existing:
	$(MAKE) run-hw-existing VARIANT=hls ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"
	$(MAKE) run-hw-existing VARIANT=hybrid ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"
	$(MAKE) run-hw-existing VARIANT=chisel_core ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"

run-sw-hls:
	$(MAKE) run-sw VARIANT=hls ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"

run-sw-hybrid:
	$(MAKE) run-sw VARIANT=hybrid ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"

run-sw-chisel-core:
	$(MAKE) run-sw VARIANT=chisel_core ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"

run-hw-hls:
	$(MAKE) run-hw VARIANT=hls ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"

run-hw-hybrid:
	$(MAKE) run-hw VARIANT=hybrid ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"

run-hw-chisel-core:
	$(MAKE) run-hw VARIANT=chisel_core ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX) TIMING=$(TIMING) REPEAT=$(REPEAT) WARMUP=$(WARMUP) HOST_ARGS="$(HOST_ARGS)"

check: run-sw run-hw

check-hls:
	$(MAKE) check VARIANT=hls ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX)

check-hybrid:
	$(MAKE) check VARIANT=hybrid ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX)

check-chisel-core:
	$(MAKE) check VARIANT=chisel_core ROWS=$(ROWS) SCALE=$(SCALE) X0=$(X0) DEVICE_INDEX=$(DEVICE_INDEX)

TEST_MODE ?= sw

test-all-variants:
	bash scripts/test-all-variants.sh "$(TEST_MODE)" "$(ROWS)" "$(SCALE)" "$(X0)"

tmux-build:
	@mkdir -p logs
	tmux new -d -s project-x-u55c-build "cd $(PROJECT_ROOT) && bash scripts/run-build-with-log.sh $(TARGET) $(VARIANT) $(DEVICE)"
	@echo "tmux session: project-x-u55c-build"
	@echo "attach with:  tmux attach -t project-x-u55c-build"
	@echo "detach with:  Ctrl-b d"

chisel: $(CHISEL_STAMP) $(CHISEL_OUTER_IP_V) $(CHISEL_OUTER_IP_RFS) $(CHISEL_SPMV_ROW_DMUL_IP_V) $(CHISEL_SPMV_ROW_DADD_IP_V) $(CHISEL_SPMV_ROW_IP_RFS)

# 第一步：用 sbt/Chisel 生成 wrapper Verilog 和 HLS companion 文件。
# 注意这里还不生成 Vivado floating_point IP 的 RTL；IP RTL 由下一条规则调用
# Vivado batch Tcl 生成。
$(CHISEL_STAMP): $(CHISEL_SRCS) $(CHISEL_DIR)/build.sbt $(CHISEL_DIR)/project/build.properties | chisel-env
	cd $(CHISEL_DIR) && JAVA_HOME="$(CHISEL_JAVA_HOME)" PATH="$(CHISEL_JAVA_HOME)/bin:$$PATH" sbt "runMain projectx.GenerateAll"
	@mkdir -p $(dir $@)
	@touch $@

$(CHISEL_OUTER_V) $(CHISEL_OUTER_HPP) $(CHISEL_OUTER_MODEL) $(CHISEL_OUTER_JSON) $(CHISEL_OUTER_IP_TCL): $(CHISEL_STAMP)

$(CHISEL_SPMV_ROW_V) $(CHISEL_SPMV_ROW_HPP) $(CHISEL_SPMV_ROW_MODEL) $(CHISEL_SPMV_ROW_JSON) $(CHISEL_SPMV_ROW_IP_TCL): $(CHISEL_STAMP)

# 第二步：用 Vivado 生成 Xilinx Floating Point IP 的 RTL 文件。
# outer_product_tile.json 会把这些 RTL 文件列进 rtl_files，供 Vitis HLS 打包进 xo。
$(CHISEL_OUTER_IP_V) $(CHISEL_OUTER_IP_RFS): $(CHISEL_OUTER_IP_TCL) | env
	@mkdir -p $(CHISEL_VIVADO_LOG_DIR)
	$(VIVADO) -mode batch -log "$(CHISEL_VIVADO_LOG_DIR)/chisel_outer_ip.log" -journal "$(CHISEL_VIVADO_LOG_DIR)/chisel_outer_ip.jou" -source $(CHISEL_OUTER_IP_TCL)

$(CHISEL_SPMV_ROW_DMUL_IP_V) $(CHISEL_SPMV_ROW_DADD_IP_V) $(CHISEL_SPMV_ROW_IP_RFS): $(CHISEL_SPMV_ROW_IP_TCL) | env
	@mkdir -p $(CHISEL_VIVADO_LOG_DIR)
	$(VIVADO) -mode batch -log "$(CHISEL_VIVADO_LOG_DIR)/chisel_spmv_row_ips.log" -journal "$(CHISEL_VIVADO_LOG_DIR)/chisel_spmv_row_ips.jou" -source $(CHISEL_SPMV_ROW_IP_TCL)

vivado-open vivado-project: vivado-env x11-check
	@test -f "$(VIVADO_XPR)" || (echo "ERROR: Vivado project not found: $(VIVADO_XPR). Build hardware first with: make build TARGET=hw" && exit 1)
	@mkdir -p $(VIVADO_LOG_DIR)
	@echo "Vivado GUI scale: $(VIVADO_SCALE)"
	@echo "Opening Vivado project: $(VIVADO_XPR)"
	$(VIVADO_GUI_ENV) $(VIVADO) -log "$(VIVADO_LOG_DIR)/vivado-open.log" -journal "$(VIVADO_LOG_DIR)/vivado-open.jou" "$(VIVADO_XPR)"

vivado-routed: vivado-env x11-check
	@test -f "$(VIVADO_ROUTED_DCP)" || (echo "ERROR: routed checkpoint not found: $(VIVADO_ROUTED_DCP). Build hardware first with: make build TARGET=hw" && exit 1)
	@mkdir -p $(BUILD_ROOT) $(VIVADO_LOG_DIR)
	@printf 'open_checkpoint {%s}\n' "$(VIVADO_ROUTED_DCP)" > "$(VIVADO_OPEN_ROUTED_TCL)"
	@echo "Vivado GUI scale: $(VIVADO_SCALE)"
	@echo "Opening routed checkpoint: $(VIVADO_ROUTED_DCP)"
	$(VIVADO_GUI_ENV) $(VIVADO) -mode gui -log "$(VIVADO_LOG_DIR)/vivado-routed.log" -journal "$(VIVADO_LOG_DIR)/vivado-routed.jou" -source "$(VIVADO_OPEN_ROUTED_TCL)"

vivado-package vivado-package-min:
	@test -f "$(VIVADO_ROUTED_DCP)" || (echo "ERROR: routed checkpoint not found: $(VIVADO_ROUTED_DCP). Build hardware first with: make build TARGET=hw" && exit 1)
	@test -d "$(VIVADO_REPORT_DIR)" || (echo "ERROR: hardware report directory not found: $(VIVADO_REPORT_DIR). Build hardware first with: make build TARGET=hw" && exit 1)
	@mkdir -p "$(VIVADO_PACKAGE_DIR)"
	cd "$(PROJECT_ROOT)" && tar -czf "$(VIVADO_PACKAGE_MIN)" \
		"$(patsubst $(PROJECT_ROOT)/%,%,$(VIVADO_ROUTED_DCP))" \
		"reports/$(VARIANT)/hw/$(DEVICE)" \
		"docs/Vivado设计查看教程_zh.md" \
		"docs/README.md" \
		"scripts/download-vivado-view.sh" \
		"scripts/download-vivado-view.ps1" \
		"README.md"
	@echo "Created: $(VIVADO_PACKAGE_MIN)"
	@ls -lh "$(VIVADO_PACKAGE_MIN)"

vivado-package-full:
	@test -d "$(VIVADO_LINK_DIR)" || (echo "ERROR: Vivado link directory not found: $(VIVADO_LINK_DIR). Build hardware first with: make build TARGET=hw" && exit 1)
	@test -d "$(VIVADO_REPORT_DIR)" || (echo "ERROR: hardware report directory not found: $(VIVADO_REPORT_DIR). Build hardware first with: make build TARGET=hw" && exit 1)
	@mkdir -p "$(VIVADO_PACKAGE_DIR)"
	cd "$(PROJECT_ROOT)" && tar -czf "$(VIVADO_PACKAGE_FULL)" \
		"build/$(VARIANT)/hw/$(DEVICE)/_x_temp/link/vivado/vpl" \
		"reports/$(VARIANT)/hw/$(DEVICE)" \
		"docs/Vivado设计查看教程_zh.md" \
		"docs/README.md" \
		"scripts/download-vivado-view.sh" \
		"scripts/download-vivado-view.ps1" \
		"README.md"
	@echo "Created: $(VIVADO_PACKAGE_FULL)"
	@ls -lh "$(VIVADO_PACKAGE_FULL)"

PACKAGE_MODE ?= both

vivado-package-all:
	bash scripts/package-all-variants.sh "$(PACKAGE_MODE)"

clean:
	rm -rf $(BUILD_ROOT) reports .Xil *.log *.jou *.csv *.run_summary

cleanall: clean
	rm -rf logs $(CHISEL_DIR)/generated $(CHISEL_DIR)/target $(CHISEL_DIR)/project/target
