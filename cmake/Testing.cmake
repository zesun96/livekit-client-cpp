include_guard(GLOBAL)

include(FetchContent)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

option(USE_SYSTEM_GTEST "Use a preinstalled GoogleTest package" OFF)

if(USE_SYSTEM_GTEST)
  find_package(GTest CONFIG REQUIRED)
elseif(NOT TARGET GTest::gtest_main)
  set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(_livekit_gtest_shared_crt ON)
  if(LKC_MSVC_RUNTIME STREQUAL "static")
    set(_livekit_gtest_shared_crt OFF)
  endif()
  set(gtest_force_shared_crt ${_livekit_gtest_shared_crt} CACHE BOOL "" FORCE)

  set(_livekit_saved_build_shared_libs ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    URL_HASH SHA512=9046841044a2bf7edfd96854ad9c44ffae4fcb9fb59a075b367507c0762a98eb32cb6968d46663228272e26321e96f4dd287c95baa22c6af9bad902b8b6ede4e
  )
  FetchContent_MakeAvailable(googletest)
  set(BUILD_SHARED_LIBS ${_livekit_saved_build_shared_libs})
endif()

include(GoogleTest)
