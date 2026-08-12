# Lightweight, reproducible dependency archives.  These avoid cloning full
# upstream Git repositories and their histories during configuration.

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

if(NOT TARGET plog::plog)
  if(USE_SYSTEM_PLOG)
    find_package(plog CONFIG REQUIRED)
  else()
    FetchContent_Declare(
      plog
      URL https://github.com/SergiusTheBest/plog/archive/refs/tags/1.1.11.tar.gz
      URL_HASH SHA256=d60b8b35f56c7c852b7f00f58cbe9c1c2e9e59566c5b200512d0cdbb6309a7c2
    )
    FetchContent_MakeAvailable(plog)
  endif()
endif()

if(NOT TARGET nlohmann_json::nlohmann_json)
  if(USE_SYSTEM_JSON)
    find_package(nlohmann_json 3.2.0 CONFIG REQUIRED)
  else()
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install OFF CACHE INTERNAL "")
    FetchContent_Declare(
      nlohmann_json
      URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
      URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    )
    FetchContent_MakeAvailable(nlohmann_json)
  endif()
endif()

# Keep the protocol revision aligned with the C++ client source.  Only the
# protobuf schemas are consumed from this archive.
FetchContent_Declare(
  livekit_protocol
  URL https://github.com/livekit/protocol/archive/refs/tags/v1.50.4.tar.gz
  URL_HASH SHA256=a2aba0546975f51badd7f91f3cebb7dcc94df6801b9d359af6ee52b14705733b
)
FetchContent_MakeAvailable(livekit_protocol)
