cmake_minimum_required(VERSION 3.20)
project(livekit_client_lua LANGUAGES CXX)

find_package(Lua REQUIRED)
find_package(Threads REQUIRED)
find_package(LiveKitClient CONFIG REQUIRED)
if(NOT LiveKitClient_SHARED)
  message(FATAL_ERROR "The Lua module requires a shared LiveKitClient package")
endif()

add_library(livekit_client_native MODULE src/livekit_lua.cpp)
target_compile_features(livekit_client_native PRIVATE cxx_std_20)
target_include_directories(livekit_client_native PRIVATE ${LUA_INCLUDE_DIR})
target_link_libraries(livekit_client_native PRIVATE ${LUA_LIBRARIES} Threads::Threads
  LiveKitClient::livekitclient)
target_compile_definitions(livekit_client_native PRIVATE LKC_SHARED)
if(MSVC)
  target_compile_options(livekit_client_native PRIVATE /EHsc)
endif()
set_target_properties(livekit_client_native PROPERTIES PREFIX "" OUTPUT_NAME "livekit_client_native")

include(GNUInstallDirs)
install(TARGETS livekit_client_native LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/lua
  RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/lua)
install(FILES lua/livekit-client.lua lua/livekit.lua DESTINATION ${CMAKE_INSTALL_DATADIR}/lua)
