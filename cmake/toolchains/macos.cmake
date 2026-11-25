# Cross-compilation toolchain: macOS from Windows host.
# Requires:
#   - LLVM/Clang installed and in PATH (clang, clang++)
#   - Env MACOS_SDK_PATH pointing to a macOS SDK (MacOSX*.sdk directory)
#   - Uncompressed sysroot. Apple's license requires the SDK be copied from
#     a Mac you own. osxcross / WinToMacApps provide helper scripts.
#
# Used via: -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=<this file>
# Arch comes from the preset cache var SPXCORE_OSX_ARCH (x86_64 or arm64).

if(NOT DEFINED ENV{MACOS_SDK_PATH})
  message(FATAL_ERROR
    "MACOS_SDK_PATH env var not set.\n"
    "Point it to a macOS SDK directory, e.g. SDK/MacOSX14.5.sdk.")
endif()
if(NOT DEFINED SPXCORE_OSX_ARCH)
  set(SPARC x86_64) # ponytail: default arch; presets set arm64 explicitly
else()
  set(SPARC ${SPXCORE_OSX_ARCH})
endif()

set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR ${SPARC})

set(CMAKE_SYSROOT $ENV{MACOS_SDK_PATH})

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET ${SPARC}-apple-darwin)
set(CMAKE_CXX_COMPILER_TARGET ${SPARC}-apple-darwin)
set(CMAKE_AR llvm-ar CACHE STRING "")
set(CMAKE_RANLIB llvm-ranlib CACHE STRING "")

set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# -stdint-pch etc. not needed; libc++ headers come from the SDK.
add_compile_options(-isystem ${CMAKE_SYSROOT}/usr/include/c++/v1)
