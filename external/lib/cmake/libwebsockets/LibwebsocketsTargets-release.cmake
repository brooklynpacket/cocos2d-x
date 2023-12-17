#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "websockets" for configuration "Release"
set_property(TARGET websockets APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(websockets PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LINK_INTERFACE_LIBRARIES_RELEASE "/Users/ianclarke/Downloads/cocos2d-x-3rd-party-libs-src-3/contrib/install-mac/arm64/lib/libz.a;/Users/ianclarke/Downloads/cocos2d-x-3rd-party-libs-src-3/contrib/install-mac/arm64/lib/libssl.a;/Users/ianclarke/Downloads/cocos2d-x-3rd-party-libs-src-3/contrib/install-mac/arm64/lib/libcrypto.a;/Users/ianclarke/Downloads/cocos2d-x-3rd-party-libs-src-3/contrib/install-mac/arm64/lib/libuv.dylib;m"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libwebsockets.a"
  )

list(APPEND _cmake_import_check_targets websockets )
list(APPEND _cmake_import_check_files_for_websockets "${_IMPORT_PREFIX}/lib/libwebsockets.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
