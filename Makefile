# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
# MIFM1: what runs on a computer. The firmware itself is built by fm1-base (base/, a submodule): make firmware.
CXX ?= c++
CC ?= cc
BASE ?= base
FLAGS = -O2 -ffp-contract=off -DTEST -DPLAT_HOST -fno-exceptions -fno-rtti -fno-threadsafe-statics
INC = -Ibench -Ibench/third_party -I$(BASE)/include
OUT = build
SRC = $(wildcard bench/mi/*.cc) $(shell find bench/third_party -name '*.cc')
OBJ = $(patsubst %.cc,$(OUT)/obj/%.o,$(SRC))

.PHONY: all test device-check firmware host clean
all: test

$(OUT)/obj/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) -std=gnu++11 $(FLAGS) -w $(INC) -c $< -o $@

$(OUT)/native: bench/tests/native.cc $(OBJ)
	$(CXX) -std=gnu++11 $(FLAGS) -Wall $(INC) -c bench/tests/native.cc -o $(OUT)/obj/native.o
	$(CC) $(OUT)/obj/native.o $(OBJ) -lm -o $@

# every scene on the computer: plays, no NaN, not silent; WAVs in build/wav
test: $(OUT)/native
	@mkdir -p $(OUT)/wav
	$(OUT)/native $(OUT)/wav 3

# the FM-1's constraints checked with the computer's clang: no C++ library, no libm, a float-only FPU (any double
# left in the code links soft-double routines and shows here as an undefined symbol)
device-check:
	sh tools/device_check.sh

# the firmware (JieLi toolchain and AC79 SDK: see base/README.md): bench/build/mifm1.fwsc
firmware:
	cd $(BASE) && python3 tools/build.py ../bench

# fm1-base's host simulator, with the real platform code
host:
	cd $(BASE) && python3 tools/build.py ../bench --host

clean:
	rm -rf $(OUT) bench/build
