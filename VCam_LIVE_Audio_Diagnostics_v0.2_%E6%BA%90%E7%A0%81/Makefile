# Standalone TikTok application-process diagnostic; one arm64 build covers
# ordinary App Store app processes on both A11 and A13 devices.
ARCHS = arm64
TARGET = iphone:clang:16.5:15.0
THEOS_PACKAGE_SCHEME = rootless

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = VCamLiveProbe
VCamLiveProbe_FILES = Tweak.c
VCamLiveProbe_CFLAGS = -std=c11 -O2 -Wall -Wextra
VCamLiveProbe_LDFLAGS = -Wl,-undefined,dynamic_lookup

include $(THEOS_MAKE_PATH)/tweak.mk
