if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "LKC_ENABLE_VLD requires Windows and the MSVC toolchain")
endif()
if(NOT LKC_VLD_ROOT)
  message(FATAL_ERROR "LKC_ENABLE_VLD requires LKC_VLD_ROOT")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_lkc_vld_arch Win64)
  set(_lkc_vld_dll vld_x64.dll)
else()
  set(_lkc_vld_arch Win32)
  set(_lkc_vld_dll vld_x86.dll)
endif()

set(_lkc_vld_include "${LKC_VLD_ROOT}/include/vld.h")
set(_lkc_vld_library "${LKC_VLD_ROOT}/lib/${_lkc_vld_arch}/vld.lib")
set(_lkc_vld_runtime "${LKC_VLD_ROOT}/bin/${_lkc_vld_arch}/${_lkc_vld_dll}")
set(_lkc_vld_dbghelp "${LKC_VLD_ROOT}/bin/${_lkc_vld_arch}/dbghelp.dll")
set(_lkc_vld_manifest
    "${LKC_VLD_ROOT}/bin/${_lkc_vld_arch}/Microsoft.DTfW.DHL.manifest")
foreach(_lkc_vld_file IN ITEMS
    "${_lkc_vld_include}"
    "${_lkc_vld_library}"
    "${_lkc_vld_runtime}"
    "${_lkc_vld_dbghelp}"
    "${_lkc_vld_manifest}")
  if(NOT EXISTS "${_lkc_vld_file}")
    message(FATAL_ERROR "Visual Leak Detector file was not found: ${_lkc_vld_file}")
  endif()
endforeach()

add_library(lkc_vld_runtime INTERFACE)
target_include_directories(lkc_vld_runtime INTERFACE "${LKC_VLD_ROOT}/include")
target_compile_options(lkc_vld_runtime INTERFACE
  "$<$<CONFIG:Debug>:/FI${_lkc_vld_include}>"
)
target_link_libraries(lkc_vld_runtime INTERFACE
  "$<$<CONFIG:Debug>:${_lkc_vld_library}>"
)

function(lkc_enable_vld target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "Cannot enable Visual Leak Detector for unknown target: ${target}")
  endif()
  target_link_libraries("${target}" PRIVATE lkc_vld_runtime)
  add_custom_command(TARGET "${target}" POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_lkc_vld_runtime}" "$<TARGET_FILE_DIR:${target}>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_lkc_vld_dbghelp}" "$<TARGET_FILE_DIR:${target}>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_lkc_vld_manifest}" "$<TARGET_FILE_DIR:${target}>"
    COMMENT "Deploying Visual Leak Detector runtime for ${target}"
    VERBATIM
  )
endfunction()

message(STATUS "Visual Leak Detector enabled from ${LKC_VLD_ROOT} (${_lkc_vld_arch}, Debug only)")
