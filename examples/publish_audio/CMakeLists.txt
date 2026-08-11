cmake_minimum_required(VERSION 3.8)

project(publish_audio_project)

add_executable(publish_audio publish_audio.cpp)

target_include_directories(publish_audio PRIVATE ${dr_libs_SOURCE_DIR})
 
target_link_libraries(publish_audio PRIVATE livekitclient)

if(MSVC)
	add_custom_command(TARGET publish_audio POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
		 ${CMAKE_SOURCE_DIR}/resources/audio/change-sophie.wav
		$<TARGET_FILE_DIR:publish_audio>
    COMMAND_EXPAND_LISTS
	)
endif()
