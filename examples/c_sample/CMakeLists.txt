add_executable(c_sample sample.c)
set_target_properties(c_sample PROPERTIES LINKER_LANGUAGE CXX)
target_compile_features(c_sample PRIVATE c_std_11)
target_link_libraries(c_sample PRIVATE livekitclient)
