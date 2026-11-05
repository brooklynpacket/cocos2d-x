# glslang (GLSL -> SPIR-V) for the Vulkan backend, compiled from the copy the
# NDK ships in sources/third_party/shaderc/third_party/glslang. The NDK's own
# Android.mk for it generates build_info.h with the host Python; this module
# uses the pre-generated header in include/ instead (glslang 11.11.0, from
# NDK 28.1). If the NDK's glslang version changes, regenerate it with:
#   python3 $GLSLANG/build_info.py $GLSLANG -i $GLSLANG/build_info.h.tmpl -o include/glslang/build_info.h

LOCAL_PATH := $(call my-dir)
CC_GLSLANG_ROOT := $(NDK_ROOT)/sources/third_party/shaderc/third_party/glslang

include $(CLEAR_VARS)
LOCAL_MODULE := cc_glslang
LOCAL_MODULE_FILENAME := libcc_glslang
LOCAL_CPPFLAGS := -std=c++17 -DGLSLANG_OSINCLUDE_UNIX -DENABLE_HLSL -w
LOCAL_C_INCLUDES := $(CC_GLSLANG_ROOT) $(CC_GLSLANG_ROOT)/glslang/MachineIndependent $(LOCAL_PATH)/include
LOCAL_EXPORT_C_INCLUDES := $(CC_GLSLANG_ROOT)
LOCAL_SRC_FILES := \
    $(CC_GLSLANG_ROOT)/glslang/CInterface/glslang_c_interface.cpp \
    $(CC_GLSLANG_ROOT)/glslang/GenericCodeGen/CodeGen.cpp \
    $(CC_GLSLANG_ROOT)/glslang/GenericCodeGen/Link.cpp \
    $(CC_GLSLANG_ROOT)/glslang/HLSL/hlslAttributes.cpp \
    $(CC_GLSLANG_ROOT)/glslang/HLSL/hlslGrammar.cpp \
    $(CC_GLSLANG_ROOT)/glslang/HLSL/hlslOpMap.cpp \
    $(CC_GLSLANG_ROOT)/glslang/HLSL/hlslParseables.cpp \
    $(CC_GLSLANG_ROOT)/glslang/HLSL/hlslParseHelper.cpp \
    $(CC_GLSLANG_ROOT)/glslang/HLSL/hlslScanContext.cpp \
    $(CC_GLSLANG_ROOT)/glslang/HLSL/hlslTokenStream.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/attribute.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/Constant.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/glslang_tab.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/InfoSink.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/Initialize.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/Intermediate.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/intermOut.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/IntermTraverse.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/iomapper.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/limits.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/linkValidate.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/parseConst.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/ParseContextBase.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/ParseHelper.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/PoolAlloc.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/propagateNoContraction.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/reflection.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/RemoveTree.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/Scan.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/ShaderLang.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/SpirvIntrinsics.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/SymbolTable.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/Versions.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/preprocessor/PpAtom.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/preprocessor/PpContext.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/preprocessor/Pp.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/preprocessor/PpScanner.cpp \
    $(CC_GLSLANG_ROOT)/glslang/MachineIndependent/preprocessor/PpTokens.cpp \
    $(CC_GLSLANG_ROOT)/SPIRV/CInterface/spirv_c_interface.cpp \
    $(CC_GLSLANG_ROOT)/SPIRV/GlslangToSpv.cpp \
    $(CC_GLSLANG_ROOT)/SPIRV/InReadableOrder.cpp \
    $(CC_GLSLANG_ROOT)/SPIRV/Logger.cpp \
    $(CC_GLSLANG_ROOT)/SPIRV/SPVRemapper.cpp \
    $(CC_GLSLANG_ROOT)/SPIRV/SpvBuilder.cpp \
    $(CC_GLSLANG_ROOT)/SPIRV/SpvPostProcess.cpp \
    $(CC_GLSLANG_ROOT)/SPIRV/SpvTools.cpp \
    $(CC_GLSLANG_ROOT)/SPIRV/disassemble.cpp \
    $(CC_GLSLANG_ROOT)/SPIRV/doc.cpp \
    $(CC_GLSLANG_ROOT)/glslang/OSDependent/Unix/ossource.cpp \
    $(CC_GLSLANG_ROOT)/OGLCompilersDLL/InitializeDll.cpp
include $(BUILD_STATIC_LIBRARY)
