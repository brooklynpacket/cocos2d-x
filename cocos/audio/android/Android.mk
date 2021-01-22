LOCAL_PATH := $(call my-dir)

#New AudioEngine
include $(CLEAR_VARS)

LOCAL_MODULE := audio

LOCAL_MODULE_FILENAME := libaudio

#turn off thumb for extra speed
LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES := ../AudioEngine.cpp \
                   ccdandroidUtils.cpp \
                   AudioEngine-inl.cpp \
                   CCThreadPool.cpp \
                   AssetFd.cpp \
                   AudioDecoder.cpp \
                   AudioDecoderProvider.cpp \
                   AudioDecoderSLES.cpp \
                   AudioDecoderOgg.cpp \
                   AudioDecoderMp3.cpp \
                   AudioDecoderWav.cpp \
                   AudioPlayerProvider.cpp \
                   AudioResampler.cpp \
                   AudioResamplerCubic.cpp \
                   PcmBufferProvider.cpp \
                   PcmAudioPlayer.cpp \
                   UrlAudioPlayer.cpp \
                   PcmData.cpp \
                   AudioMixerController.cpp \
                   AudioMixer.cpp \
                   PcmAudioService.cpp \
                   Track.cpp \
                   audio_utils/format.c \
                   audio_utils/minifloat.cpp \
                   audio_utils/primitives.c \
                   utils/Utils.cpp \
                   mp3reader.cpp \
                   tinysndfile.cpp 

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/../include

LOCAL_EXPORT_LDLIBS := -lOpenSLES

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../include \
                    $(LOCAL_PATH)/../.. \
                    $(LOCAL_PATH)/../../platform/android

LOCAL_STATIC_LIBRARIES := ext_vorbisidec \
                          ext_pvmp3dec


include $(BUILD_STATIC_LIBRARY)

#SimpleAudioEngine
include $(CLEAR_VARS)

LOCAL_MODULE := ccds

LOCAL_MODULE_FILENAME := libccds

#turn off thumb for extra speed
LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES := $(LOCAL_PATH)/../../editor-support/cocostudio/SimpleAudioEngine.cpp \
                   ccdandroidUtils.cpp

LOCAL_STATIC_LIBRARIES := audio

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/../include

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../include \
                    $(LOCAL_PATH)/../.. \
                    $(LOCAL_PATH)/../../platform/android \
                    $(LOCAL_PATH)/../../editor-support/cocostudio/

include $(BUILD_STATIC_LIBRARY)
