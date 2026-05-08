SHELL := /bin/bash

PYTHON ?= python3
CXX ?= g++
SIZE ?= 512
TAU ?= 1e-20
CHECK_TOLERANCE ?= 1e-8
MAX_ITERS ?=
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -pedantic

ROOT_DIR := $(abspath .)
TESTDATA_DIR := $(abspath $(ROOT_DIR)/..)
BUILD_DIR := $(ROOT_DIR)/build
VERIFY_EXE := $(BUILD_DIR)/cgsolver_verify
DATASET_ROOT := $(TESTDATA_DIR)/generated/cgsolver/datasets
DATASET_DIR := $(DATASET_ROOT)/n$(SIZE)
GENERATE_ARGS := --size $(SIZE) --output-dir $(DATASET_DIR) --tau $(TAU) --check-tolerance $(CHECK_TOLERANCE)

ifneq ($(strip $(MAX_ITERS)),)
GENERATE_ARGS += --max-iters $(MAX_ITERS)
endif

.PHONY: all generate build test clean

all: test

generate:
	$(PYTHON) $(ROOT_DIR)/generate_dataset.py $(GENERATE_ARGS)

build: $(VERIFY_EXE)

$(VERIFY_EXE): $(ROOT_DIR)/cgsolver_verify.cpp $(shell find $(ROOT_DIR)/include -type f | sort) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(ROOT_DIR)/include $< -o $@

$(BUILD_DIR):
	mkdir -p $@

test: generate build
	$(VERIFY_EXE) $(DATASET_DIR)

clean:
	rm -rf $(BUILD_DIR)
