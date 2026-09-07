LOCAL_PATH := $(call my-dir)

# API library; this is the library that most programs should use
include $(CLEAR_VARS)
LOCAL_MODULE := libvideo-scale
LOCAL_CATEGORY_PATH := libs
LOCAL_DESCRIPTION := Video scaling library
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_CFLAGS := -DVSCALE_API_EXPORTS -fvisibility=hidden -std=gnu99 -D_GNU_SOURCE
LOCAL_SRC_FILES := \
	src/vscale.c
LOCAL_LIBRARIES := \
	libpomp \
	libulog \
	libmedia-buffers \
	libvideo-defs \
	libvideo-scale-core
LOCAL_CONFIG_FILES := config.in
$(call load-config)
LOCAL_CONDITIONAL_LIBRARIES := \
	CONFIG_VSCALE_HISI:libvideo-scale-hisi \
	CONFIG_VSCALE_QCOM:libvideo-scale-qcom \
	CONFIG_VSCALE_LIBYUV:libvideo-scale-libyuv
LOCAL_EXPORT_LDLIBS := -lvideo-scale-core
include $(BUILD_LIBRARY)

# Core library: common code for all implementations and structures definitions;
# used by implementations
include $(CLEAR_VARS)
LOCAL_MODULE := libvideo-scale-core
LOCAL_CATEGORY_PATH := libs
LOCAL_DESCRIPTION := Video scaling library: core files
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/core/include
LOCAL_CFLAGS := -DVSCALE_API_EXPORTS -fvisibility=hidden -std=gnu99 -D_GNU_SOURCE
LOCAL_SRC_FILES := \
	core/src/vscale_core.c \
	core/src/vscale_enums.c
LOCAL_LIBRARIES := \
	libfutils \
	libulog \
	libmedia-buffers \
	libvideo-defs
include $(BUILD_LIBRARY)

# libyuv implementation; can be enabled in the product configuration
include $(CLEAR_VARS)
LOCAL_MODULE := libvideo-scale-libyuv
LOCAL_CATEGORY_PATH := libs
LOCAL_DESCRIPTION := Video scaling library: libyuv implementation
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/libyuv/include
LOCAL_CFLAGS := -DVSCALE_API_EXPORTS -fvisibility=hidden -std=gnu11 -D_GNU_SOURCE
LOCAL_SRC_FILES := \
	libyuv/src/vscale_libyuv.c
LOCAL_LIBRARIES := \
	libfutils \
	libmedia-buffers \
	libmedia-buffers-memory \
	libmedia-buffers-memory-generic \
	libpomp \
	libulog \
	libvideo-defs \
	libvideo-metadata \
	libvideo-scale-core \
	libyuv
include $(BUILD_LIBRARY)

# Scaling program
include $(CLEAR_VARS)
LOCAL_MODULE := vscale
LOCAL_DESCRIPTION := Video scaling program
LOCAL_CATEGORY_PATH := multimedia
LOCAL_CFLAGS := -std=gnu11
LOCAL_SRC_FILES := \
	tools/vscale.c
LOCAL_LIBRARIES := \
	libfutils \
	libmedia-buffers \
	libmedia-buffers-memory \
	libmedia-buffers-memory-generic \
	libpomp \
	libulog \
	libvideo-defs \
	libvideo-raw \
	libvideo-scale
include $(BUILD_EXECUTABLE)

ifdef TARGET_TEST

include $(CLEAR_VARS)
LOCAL_MODULE := tst-libvideo-scale
LOCAL_CFLAGS += -DTARGET_TEST -D_GNU_SOURCE
ifeq ("$(CONFIG_VSCALE_LIBYUV)","y")
LOCAL_CFLAGS += -DBUILD_LIBVIDEO_SCALE_LIBYUV
endif
LOCAL_SRC_FILES := \
	tests/vscale_test.c \
	tests/vscale_test_core.c \
	tests/vscale_test_config.c \
	tests/vscale_test_lifecycle.c
LOCAL_LIBRARIES := \
	libcunit \
	libvideo-scale \
	libvideo-scale-core \
	libfutils \
	libpomp \
	libulog \
	libvideo-defs \
	libvideo-metadata \
	libmedia-buffers \
	libmedia-buffers-memory \
	libmedia-buffers-memory-generic
include $(BUILD_EXECUTABLE)

endif
