# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.0

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list

# Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/local/Cellar/cmake/3.0.2/bin/cmake

# The command to remove a file.
RM = /usr/local/Cellar/cmake/3.0.2/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /Users/danieldionne/git/fg/lib/griffin/lib/cocos2d-x/external/emsdk_portable/clang/tag-e1.34.1/src

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /Users/danieldionne/git/fg/lib/griffin/lib/cocos2d-x/external/emsdk_portable/clang/tag-e1.34.1/build_tag-e1.34.1_64

# Include any dependencies generated for this target.
include tools/llvm-as/CMakeFiles/llvm-as.dir/depend.make

# Include the progress variables for this target.
include tools/llvm-as/CMakeFiles/llvm-as.dir/progress.make

# Include the compile flags for this target's objects.
include tools/llvm-as/CMakeFiles/llvm-as.dir/flags.make

tools/llvm-as/CMakeFiles/llvm-as.dir/llvm-as.cpp.o: tools/llvm-as/CMakeFiles/llvm-as.dir/flags.make
tools/llvm-as/CMakeFiles/llvm-as.dir/llvm-as.cpp.o: /Users/danieldionne/git/fg/lib/griffin/lib/cocos2d-x/external/emsdk_portable/clang/tag-e1.34.1/src/tools/llvm-as/llvm-as.cpp
	$(CMAKE_COMMAND) -E cmake_progress_report /Users/danieldionne/git/fg/lib/griffin/lib/cocos2d-x/external/emsdk_portable/clang/tag-e1.34.1/build_tag-e1.34.1_64/CMakeFiles $(CMAKE_PROGRESS_1)
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Building CXX object tools/llvm-as/CMakeFiles/llvm-as.dir/llvm-as.cpp.o"
	cd /Users/danieldionne/git/fg/lib/griffin/lib/cocos2d-x/external/emsdk_portable/clang/tag-e1.34.1/build_tag-e1.34.1_64/tools/llvm-as && /usr/bin/c++   $(CXX_DEFINES) $(CXX_FLAGS) -o CMakeFiles/llvm-as.dir/llvm-as.cpp.o -c /Users/danieldionne/git/fg/lib/griffin/lib/cocos2d-x/external/emsdk_portable/clang/tag-e1.34.1/src/tools/llvm-as/llvm-as.cpp

tools/llvm-as/CMakeFiles/llvm-as.dir/llvm-as.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/llvm-as.dir/llvm-as.cpp.i"
	cd /Users/danieldionne/git/fg/lib/griffin/lib/cocos2d-x/external/emsdk_portable/clang/tag-e1.34.1/build_tag-e1.34.1_64/tools/llvm-as && /usr/bin/c++  $(CXX_DEFINES) $(CXX_FLAGS) -E /Users/danieldionne/git/fg/lib/griffin/lib/cocos2d-x/external/emsdk_portable/clang/tag-e1.34.1/src/tools/llvm-as/llvm-as.cpp > CMakeFiles/llvm-as.dir/llvm-as.cpp.i

tools/llvm-as/CMakeFiles/llvm-as.dir/llvm-as.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/llvm-as.dir/llvm-as.cpp.s"
	cd /Users/danieldionne/git/fg/lib/griffin/lib/cocos2d-x/external/emsdk_portable/clang/tag-e1.34.1/build_tag-e1.34.1_64/tools/llvm-as && /usr/bin/c++  $(CXX_DEFINES) $(CXX_FLAGS) -S /Users/danieldionne/git/fg/lib/griffin/lib/cocos2d-x/external/emsdk_portable/clang/tag-e1.34.1/src/tools/llvm-as/llvm-as.cpp -o CMakeFiles/llvm-as.dir/llvm-as.cpp.s

tools/llvm-as/CMakeFiles/llvm-as.dir/llvm-as.cpp.o.requires:
.PHONY : tools/llvm-as/CMakeFiles/llvm-as.dir/llvm-as.cpp.o.requires

tools/llvm-as/CMakeFiles/llvm-as.dir/llvm-as.cpp.o.provides: tools/llvm-as/CMakeFiles/llvm-as.dir/llvm-as.cpp.o.requires
	$(MAKE) -f tools/llvm-as/CMakeFiles/llvm-as.dir/build.make tools/llvm-as/CMakeFiles/llvm-as.dir/llvm-as.cpp.o.provides.build
.PHONY : tools/llvm-as/CMakeFiles/llvm-as.dir/llvm-as.cpp.o.provides

tools/llvm-as/CMakeFiles/llvm-as.dir/llvm-as.cpp.o.provides.build: tools/llvm-as/CMakeFiles/llvm-as.dir/llvm-as.cpp.o

# Object files for target llvm-as
llvm__as_OBJECTS = \
"CMakeFiles/llvm-as.dir/llvm-as.cpp.o"

# External object files for target llvm-as
llvm__as_EXTERNAL_OBJECTS =

bin/llvm-as: tools/llvm-as/CMakeFiles/llvm-as.dir/llvm-as.cpp.o
bin/llvm-as: tools/llvm-as/CMakeFiles/llvm-as.dir/build.make
bin/llvm-as: lib/libLLVMAsmParser.a
bin/llvm-as: lib/libLLVMBitWriter.a
bin/llvm-as: lib/libLLVMCore.a
bin/llvm-as: lib/libLLVMSupport.a
bin/llvm-as: tools/llvm-as/CMakeFiles/llvm-as.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --red --bold "Linking CXX executable ../../bin/llvm-as"
	cd /Users/danieldionne/git/fg/lib/griffin/lib/cocos2d-x/external/emsdk_portable/clang/tag-e1.34.1/build_tag-e1.34.1_64/tools/llvm-as && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/llvm-as.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
tools/llvm-as/CMakeFiles/llvm-as.dir/build: bin/llvm-as
.PHONY : tools/llvm-as/CMakeFiles/llvm-as.dir/build

tools/llvm-as/CMakeFiles/llvm-as.dir/requires: tools/llvm-as/CMakeFiles/llvm-as.dir/llvm-as.cpp.o.requires
.PHONY : tools/llvm-as/CMakeFiles/llvm-as.dir/requires

tools/llvm-as/CMakeFiles/llvm-as.dir/clean:
	cd /Users/danieldionne/git/fg/lib/griffin/lib/cocos2d-x/external/emsdk_portable/clang/tag-e1.34.1/build_tag-e1.34.1_64/tools/llvm-as && $(CMAKE_COMMAND) -P CMakeFiles/llvm-as.dir/cmake_clean.cmake
.PHONY : tools/llvm-as/CMakeFiles/llvm-as.dir/clean

tools/llvm-as/CMakeFiles/llvm-as.dir/depend:
	cd /Users/danieldionne/git/fg/lib/griffin/lib/cocos2d-x/external/emsdk_portable/clang/tag-e1.34.1/build_tag-e1.34.1_64 && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /Users/danieldionne/git/fg/lib/griffin/lib/cocos2d-x/external/emsdk_portable/clang/tag-e1.34.1/src /Users/danieldionne/git/fg/lib/griffin/lib/cocos2d-x/external/emsdk_portable/clang/tag-e1.34.1/src/tools/llvm-as /Users/danieldionne/git/fg/lib/griffin/lib/cocos2d-x/external/emsdk_portable/clang/tag-e1.34.1/build_tag-e1.34.1_64 /Users/danieldionne/git/fg/lib/griffin/lib/cocos2d-x/external/emsdk_portable/clang/tag-e1.34.1/build_tag-e1.34.1_64/tools/llvm-as /Users/danieldionne/git/fg/lib/griffin/lib/cocos2d-x/external/emsdk_portable/clang/tag-e1.34.1/build_tag-e1.34.1_64/tools/llvm-as/CMakeFiles/llvm-as.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : tools/llvm-as/CMakeFiles/llvm-as.dir/depend

