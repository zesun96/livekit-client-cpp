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
  URL https://github.com/livekit/protocol/archive/5bd7e73f315a4496a4f0ca778d7a94de8b432f00.tar.gz
  URL_HASH SHA256=40e3662841b6884f3e8cf1391fcb1c7bcdc343ba5964c4dbace28a7e0dc287b9
)
FetchContent_MakeAvailable(livekit_protocol)

# dr_libs is only required by the publish_audio example.  Upstream does not
# publish releases, so use a small source snapshot pinned to the prior
# submodule revision.
if(BUILD_EXAMPLES)
  FetchContent_Declare(
    dr_libs
    URL https://github.com/mackron/dr_libs/archive/9cb7092ac8c75a82b5c6ea72652ca8d0091d7ffa.tar.gz
    URL_HASH SHA256=c735e09975069d69544f8dcb4a5f8668e0300aa673382076d35e954345685dad
  )
  FetchContent_MakeAvailable(dr_libs)
endif()
