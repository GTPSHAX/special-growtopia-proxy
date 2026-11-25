# Cross-compilation toolchain: Linux x86_64 (and adjustable), from Windows host.
# Requires:
#   - LLVM/Clang installed and in PATH (clang, clang++, llvm-ar)
#   - Env LINUX_SYSROOT pointing to a linux-gnu sysroot (glibc headers + libs)
#
# Sysroot sources (pick one):
#   - Prebuilt: e.g. from a docker-exported sysroot, or Debian/Ubuntu package dump
#   - Native build in WSL, then point LINUX_SYSROOT at /usr (via \\wsl$ path)
#
# Used via: -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=<this file>

if(NOT DEFINED ENV{LINUX_SYSROOT})
  message(FATAL_ERROR
    "LINUX_SYSROOT env var not set.\n"
    "Point it to a Linux glibc sysroot directory (containing usr/include, usr/lib).")
endif()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_SYSROOT $ENV{LINUX_SYSROOT})

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET x86_64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET x86_64-linux-gnu)
set(CMAKE_AR llvm-ar CACHE STRING "")
set(CMAKE_RANLIB llvm-ranlib CACHE STRING "")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")

set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
