if(SPXCORE_BUILD_SHARED)
  add_library(specialproxy_core SHARED)
  set_target_properties(specialproxy_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
else()
  add_library(specialproxy_core STATIC)
endif()

target_sources(specialproxy_core PRIVATE
  proxy/Core.cpp
  proxy/ControlChannel.cpp
  proxy/ProxyService.cpp
  proxy/command/BuiltinCommands.cpp
  proxy/command/ChatCommandHandler.cpp
  proxy/command/CommandParser.cpp
  proxy/command/CommandRegistry.cpp
  proxy/item/ItemData.cpp
  proxy/item/ItemDatabase.cpp
  proxy/player/PlayerManager.cpp
  proxy/world/GameState.cpp
  proxy/world/WorldManager.cpp
  proxy/world/WorldParser.cpp
  script/LuaEngine.cpp
  script/Scheduler.cpp
  script/LuaPacket.cpp
  script/Script.cpp
  packet/PacketDecoder.cpp
  packet/PacketID.cpp
  packet/PacketRegistry.cpp
  net/Net.cpp
  net/Socks5.cpp
  net/Socks5Tcp.cpp
  net/HttpClient.cpp
  platforms/HostsFile.cpp
  utils/Logger.cpp
)

if(WIN32)
  target_sources(specialproxy_core PRIVATE platforms/WindowsPlatform.cpp)
  target_link_libraries(specialproxy_core PRIVATE crypt32)
else()
  target_sources(specialproxy_core PRIVATE platforms/PosixPlatform.cpp)
endif()

if(NOT ANDROID)
  target_sources(specialproxy_core PRIVATE
    proxy/https/HttpsService.cpp
    proxy/https/HttpsApi.cpp
    proxy/https/CertificateFactory.cpp
    proxy/https/DnsResolver.cpp
    proxy/https/ServerData.cpp
  )
endif()

find_package(OpenSSL REQUIRED)
target_link_libraries(specialproxy_core PUBLIC OpenSSL::SSL OpenSSL::Crypto)

target_include_directories(specialproxy_core
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LUA_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIRS}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(specialproxy_core PUBLIC enet_growtopia)
target_link_libraries(specialproxy_core PRIVATE ${LUA_LIBRARIES} ${ZLIB_LIBRARIES})

target_compile_definitions(specialproxy_core
  PRIVATE SPXCORE_VERSION="${PROJECT_VERSION}"
)
