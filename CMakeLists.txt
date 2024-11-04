# ----------------------------------------------------------------------------
#  Root CMake file for livekit-client-cpp-sdk
#
# ----------------------------------------------------------------------------
cmake_minimum_required (VERSION 3.8)

project ("livekit-client-cpp-sdk")

# C++ standard requirements.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(
	LKC_SOURCE_FILES

	src/livekit_client.cpp
)

add_library (livekitclient STATIC
	${LKC_HEADER_FILES}
	${LKC_SOURCE_FILES}
)

target_include_directories(livekitclient PRIVATE ${PROJECT_SOURCE_DIR}/include)

if(NOT BUILD_EXAMPLES)
	set(BUILD_EXAMPLES true)
endif()
# examples
if(BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()
