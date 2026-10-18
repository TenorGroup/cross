if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.16)
  project(hal_storage_trace LANGUAGES CXX)
  get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
  enable_testing()
endif()

foreach(trace_mode IN ITEMS off on)
  set(trace_target HalStorageTrace_${trace_mode})
  add_executable(${trace_target}
    ${REPO_ROOT}/lib/hal/HalStorage.cpp
    trace_test.cpp
  )
  target_compile_features(${trace_target} PRIVATE cxx_std_17)
  target_include_directories(${trace_target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${REPO_ROOT}/lib/hal
  )
  target_compile_definitions(${trace_target} PRIVATE FREEINK_CAP_USB_MSC=0)
  if(trace_mode STREQUAL "on")
    target_compile_definitions(${trace_target} PRIVATE TENOR_UI_ACCEPTANCE)
  endif()
  target_compile_options(${trace_target} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${trace_target} PRIVATE -Wno-unused-private-field)
  endif()
  add_test(NAME ${trace_target} COMMAND ${trace_target})
endforeach()
