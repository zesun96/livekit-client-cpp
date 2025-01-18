# cmake needs this line
cmake_minimum_required(VERSION 3.8)

project(cpp_sample_project)

add_executable(cpp_sample sample.cpp)

target_link_libraries(cpp_sample PRIVATE livekitclient)

if(MSVC)
	add_custom_command(TARGET cpp_sample POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
		$<TARGET_FILE_DIR:websocketclient>/websocketclient.dll
		$<TARGET_FILE_DIR:cpp_sample>
    COMMAND_EXPAND_LISTS
	)
endif()
