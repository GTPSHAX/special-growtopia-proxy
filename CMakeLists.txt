cmake_minimum_required(VERSION 3.20)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(specialproxy-core
  VERSION 0.1.0
  DESCRIPTION "Cross-platform proxy core library"
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  # The core and tests catch standard exceptions, so unwind semantics must be on.
  add_compile_options(/EHsc)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

find_package(Lua REQUIRED)
find_package(ZLIB REQUIRED)

option(SPXCORE_BUILD_TESTS "Build tests" ON)
option(SPXCORE_BUILD_EXAMPLES "Build example mini projects" ON)
option(SPXCORE_BUILD_SHARED "Build the core as a shared library" ON)
option(SPXCORE_BUILD_DISTRIBUTION_APP "Build the Windows distribution executable" OFF)

add_subdirectory(third_party/enet_growtopia)
if(WIN32)
  target_link_libraries(enet_growtopia PUBLIC ws2_32 winmm)
endif()
add_subdirectory(src)

if(SPXCORE_BUILD_EXAMPLES)
  add_subdirectory(examples/CoreTest)
endif()

if(SPXCORE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
