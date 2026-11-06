# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
# MIFM1: what runs on a computer. The firmware itself is built by fm1-base (base/, a submodule): make firmware.
CXX ?= c++
CC ?= cc
BASE ?= base
FLAGS = -O2 -ffp-contract=off -DTEST -DPLAT_HOST -fno-exceptions -fno-rtti -fno-threadsafe-statics
INC = -Ifirmware -Ifirmware/third_party -Ifirmware/third_party/lxr -I$(BASE)/include
OUT = build
SRC = $(wildcard firmware/mi/*.cc) $(shell find firmware/third_party -name '*.cc')
OBJ = $(patsubst %.cc,$(OUT)/obj/%.o,$(SRC))

.PHONY: pat-store all test lxr-check uac-multi uac-stereo base-patch device-check firmware host web clean
all: test

$(OUT)/obj/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) -std=gnu++11 $(FLAGS) -w -MMD -MP $(INC) -c $< -o $@

-include $(OBJ:.o=.d) $(OUT)/obj/native.d

$(OUT)/native: firmware/tests/native.cc $(OBJ)
	$(CXX) -std=gnu++11 $(FLAGS) -Wall -MMD -MP $(INC) -c firmware/tests/native.cc -o $(OUT)/obj/native.o
	$(CC) $(OUT)/obj/native.o $(OBJ) -lm -o $@

# the engine on a computer: the default patch plays, every track and model sounds, no NaN; WAVs in build/wav
test: $(OUT)/native lxr-check uac-multi uac-stereo pat-store
	@mkdir -p $(OUT)/wav
	$(OUT)/native $(OUT)/wav 16

# the patterns' store (firmware/pat_store.h) on a simulated flash: saves, room made, power cuts anywhere
$(OUT)/pat_store_test: firmware/tests/pat_store_test.c firmware/pat_store.h
	@mkdir -p $(OUT)
	$(CC) -std=c11 -O2 -Wall -Wextra -Wno-unused-function -Ibase/include firmware/tests/pat_store_test.c -o $@
pat-store: $(OUT)/pat_store_test
	$(OUT)/pat_store_test

# the FM drums (third_party/lxr) as the FM-1 computes them, against the LXR-02's arithmetic (doubles, fused
# multiply-adds): the same voices, sample by sample
LXR_SRC = firmware/mi/mi_lxr.cc $(wildcard firmware/third_party/lxr/*.cc firmware/third_party/lxr/Soundengine/DSPAudio/*.cc)
$(OUT)/lxr_port: tools/lxr_check.cc $(LXR_SRC)
	@mkdir -p $(OUT)
	$(CXX) -std=gnu++11 $(FLAGS) -w $(INC) tools/lxr_check.cc $(LXR_SRC) -lm -o $@
$(OUT)/lxr_ref: tools/lxr_check.cc $(LXR_SRC)
	@mkdir -p $(OUT)
	$(CXX) -std=gnu++11 $(FLAGS) -DLXR_HARD_DOUBLE -w $(INC) tools/lxr_check.cc $(LXR_SRC) -lm -o $@
lxr-check: $(OUT)/lxr_port $(OUT)/lxr_ref
	$(OUT)/lxr_ref $(OUT)/lxr_ref.raw
	$(OUT)/lxr_port $(OUT)/lxr_ref.raw

# MIFM1's changes to fm1-base (patches/), applied to base/ once
base-patch:
	sh tools/patch_base.sh

# the USB audio input's 10 channels on felucca-bsp's usb.c as patched: the descriptors, then 60 s of streaming as on
# the FM-1, every frame of every channel recorded once, in order
uac-multi: base-patch
	@mkdir -p $(OUT)
	$(CC) -std=c99 -O2 -w -I$(BASE)/bsp/chip -I$(BASE)/bsp/boards/fm1 tools/uac_multi_test.c -o $(OUT)/uac_multi_test
	$(OUT)/uac_multi_test

# (0.16.33) the USB audio input as the firmware has it: the master in stereo, two halves in hand, a render late now and then
uac-stereo: base-patch
	@mkdir -p $(OUT)
	$(CC) -std=c99 -O2 -w -I$(BASE)/bsp/chip -I$(BASE)/bsp/boards/fm1 tools/uac_stereo_test.c -o $(OUT)/uac_stereo_test
	$(OUT)/uac_stereo_test

# the browser emulator (README: Dans le navigateur): the firmware in WebAssembly, by Zig's clang (pip install ziglang)
# through tools/web/emcc, with MIFM1's page; firmware/build/emu/ (serve it over HTTP)
web: base-patch
	cd $(BASE) && PATH="$(CURDIR)/tools/web:$$PATH" python3 tools/build.py ../firmware --web
	if [ -d firmware/web/emu/img ]; then cp -r firmware/web/emu/img firmware/build/emu/; fi

# the FM-1's constraints checked with the computer's clang: no C++ library, no libm, a float-only FPU (any double
# left in the code links soft-double routines and shows here as an undefined symbol)
device-check:
	sh tools/device_check.sh

# the firmware (JieLi toolchain and AC79 SDK: see base/README.md): firmware/build/mifm1.fwsc
firmware: base-patch
	cd $(BASE) && python3 tools/build.py ../firmware

# fm1-base's host simulator, with the real platform code
host: base-patch
	cd $(BASE) && python3 tools/build.py ../firmware --host

clean:
	rm -rf $(OUT) firmware/build
