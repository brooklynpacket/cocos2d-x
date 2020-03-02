LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := cc3d
LOCAL_ARM_MODE := arm

LOCAL_MODULE_FILENAME := libc3d

#turn off thumb for extra speed
LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES := \
CC3DProgramInfo.cpp \
CCAABB.cpp \
CCAnimate3D.cpp \
CCAnimation3D.cpp \
CCAttachNode.cpp \
CCBillBoard.cpp \
CCBundle3D.cpp \
CCBundle3DData.cpp \
CCBundleReader.cpp \
CCMesh.cpp \
CCMeshSkin.cpp \
CCMeshVertexIndexData.cpp \
CCMotionStreak3D.cpp \
CCOBB.cpp \
CCObjLoader.cpp \
CCRay.cpp \
CCSkeleton3D.cpp \
CCSkybox.cpp \
CCSprite3D.cpp \
CCSprite3DMaterial.cpp \
CCTerrain.cpp \
CCVertexAttribBinding.cpp
#in the paranet makefile
#CCFrustum.cpp
#CCPlane.cpp

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/..

LOCAL_C_INCLUDES := $(LOCAL_PATH)/..

LOCAL_STATIC_LIBRARIES := cc_core

include $(BUILD_STATIC_LIBRARY)
