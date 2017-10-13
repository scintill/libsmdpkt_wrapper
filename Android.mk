LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
	LOCAL_MODULE := libsmdpkt_wrapper

	LOCAL_CFLAGS := -Wall -Wextra -Werror -std=c99 -O2
	LOCAL_SRC_FILES := wrapper.c
include $(BUILD_SHARED_LIBRARY)
