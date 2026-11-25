enable_testing()

add_executable(TestVersion TestVersion.cpp)
target_link_libraries(TestVersion PRIVATE specialproxy_core)
add_test(NAME version COMMAND TestVersion)

add_executable(TestProxyCore TestProxyCore.cpp)
target_link_libraries(TestProxyCore PRIVATE specialproxy_core)
add_test(NAME proxy_core COMMAND TestProxyCore)

add_executable(TestPackets TestPackets.cpp)
target_link_libraries(TestPackets PRIVATE specialproxy_core)
add_test(NAME packets COMMAND TestPackets)

add_executable(TestVariants TestVariants.cpp)
target_link_libraries(TestVariants PRIVATE specialproxy_core)
add_test(NAME variants COMMAND TestVariants)

add_executable(TestPacketBuilder TestPacketBuilder.cpp)
target_link_libraries(TestPacketBuilder PRIVATE specialproxy_core)
add_test(NAME packet_builder COMMAND TestPacketBuilder)

add_executable(TestCommands TestCommands.cpp)
target_link_libraries(TestCommands PRIVATE specialproxy_core)
add_test(NAME commands COMMAND TestCommands)

add_executable(TestScript TestScript.cpp)
target_include_directories(TestScript PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/script")
target_compile_definitions(TestScript PRIVATE
  SPX_SCRIPT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../bin/scripts")
target_link_libraries(TestScript PRIVATE specialproxy_core ${LUA_LIBRARIES})
add_test(NAME script COMMAND TestScript)

add_executable(TestItems TestItems.cpp)
target_include_directories(TestItems PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src"
  ${ZLIB_INCLUDE_DIRS})
target_link_libraries(TestItems PRIVATE specialproxy_core ${ZLIB_LIBRARIES})
add_test(NAME items COMMAND TestItems)

add_executable(TestState TestState.cpp)
target_link_libraries(TestState PRIVATE specialproxy_core)
add_test(NAME state COMMAND TestState)

add_executable(TestNet TestNet.cpp)
target_link_libraries(TestNet PRIVATE specialproxy_core)
add_test(NAME net COMMAND TestNet)

add_executable(TestLogger TestLogger.cpp)
target_link_libraries(TestLogger PRIVATE specialproxy_core)
add_test(NAME logger COMMAND TestLogger)

add_executable(TestPlatform TestPlatform.cpp)
target_include_directories(TestPlatform PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/platforms")
target_link_libraries(TestPlatform PRIVATE specialproxy_core)
add_test(NAME platforms COMMAND TestPlatform)

add_executable(TestHttp TestHttp.cpp)
target_include_directories(TestHttp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(TestHttp PRIVATE specialproxy_core)
add_test(NAME http COMMAND TestHttp)

if(NOT ANDROID)
  add_executable(TestHttps TestHttps.cpp)
  target_include_directories(TestHttps PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/platforms")
  target_link_libraries(TestHttps PRIVATE specialproxy_core)
  add_test(NAME https COMMAND TestHttps)
endif()

if(MSVC)
  get_property(test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(test_target IN LISTS test_targets)
    if(test_target MATCHES "^Test")
      target_compile_options(${test_target} PRIVATE /UNDEBUG)
    endif()
  endforeach()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  get_property(test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(test_target IN LISTS test_targets)
    if(test_target MATCHES "^Test")
      target_compile_options(${test_target} PRIVATE -UNDEBUG)
    endif()
  endforeach()
endif()
