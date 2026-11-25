add_executable(CoreTestProxy CoreTestProxy.cpp)
target_link_libraries(CoreTestProxy PRIVATE specialproxy_core)

if(NOT ANDROID)
  add_executable(CoreTestHttps CoreTestHttps.cpp)
  target_include_directories(CoreTestHttps PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
  target_link_libraries(CoreTestHttps PRIVATE specialproxy_core)
endif()

add_executable(CoreTestServer CoreTestServer.cpp)
target_include_directories(CoreTestServer PRIVATE ${ZLIB_INCLUDE_DIRS})
target_link_libraries(CoreTestServer PRIVATE specialproxy_core ${ZLIB_LIBRARIES})

add_executable(CoreTestClient CoreTestClient.cpp)
target_link_libraries(CoreTestClient PRIVATE specialproxy_core)

if(WIN32 AND SPXCORE_BUILD_DISTRIBUTION_APP)
  add_executable(SpecialProxy SpecialProxy.cpp)
  target_link_libraries(SpecialProxy PRIVATE specialproxy_core)
  target_link_options(SpecialProxy PRIVATE
    "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()
