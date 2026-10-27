#!/usr/bin/make -f
NAME = HARDER
FILES_DSP = HarderPlugin.cpp $(wildcard hmath/hm*.c)
FILES_UI  = HarderUI.cpp
DPF_TARGET_DIR ?= ../../bin
DPF_BUILD_DIR ?= ../../build
ifeq ($(WINDOWS),true)
# must be known before DPF's rules are read (their prerequisites are expanded at parse time)
RES_O = $(DPF_BUILD_DIR)/harder-res.o
EXTRA_DEPENDENCIES += $(RES_O)
endif
include ../../dpf/Makefile.plugins.mk
# DPF defaults to -ffast-math: that would delete the engine's NaN guards (x != x) and change rounding,
# so the DSP is built with IEEE semantics, exactly like the reference renders.
BASE_OPTS = -O3 -fno-math-errno -ffp-contract=off -fdata-sections -ffunction-sections -mtune=generic -msse -msse2 -mfpmath=sse
BUILD_C_FLAGS += -std=gnu99 -Ihmath -Ihmath/src -Wno-unused-parameter -Wno-sign-compare -Wno-implicit-fallthrough -Wno-missing-field-initializers -Wno-unused-but-set-variable
BUILD_CXX_FLAGS += -std=gnu++17 -DDPF_PARAM_TEXT_HOOK -DDPF_VST3_NO_MIDI_CC_PARAMS -DDPF_PARAM_REFUSAL -DDPF_NATIVE_AUDIO_SETTINGS -DDPF_RTAUDIO_WASAPI_NO_MF -DDPF_RTAUDIO_WASAPI_JOIN_THREAD -DDPF_WIN_SAVE_DEFAULT_NAME -Wno-misleading-indentation -fno-math-errno -Wframe-larger-than=32768
ifeq ($(WINDOWS),true)
LINK_FLAGS += -static -static-libgcc -static-libstdc++
# app icon (resource 1, see WINDOWS_ICON_ID=1 in scripts/build.sh); DGL sets it on the window
WINDRES ?= x86_64-w64-mingw32-windres
LINK_FLAGS += $(RES_O)
$(RES_O): res/harder.rc res/harder.ico
	-@mkdir -p $(dir $(RES_O))
	$(WINDRES) -I res res/harder.rc -O coff -o $@
endif
# vst3 = the plugin, jack = the standalone program (JACK if running, else the OS audio driver)
TARGETS += vst3 jack
all: $(TARGETS)
