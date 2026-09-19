find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(transport_source ${REPO_ROOT}/src/network/HttpDownloader.cpp)
set(transport_include ${CMAKE_CURRENT_BINARY_DIR}/production_transport.inc)
add_custom_command(
  OUTPUT ${transport_include}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_transport_regression.py
          --extract-only --output ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${transport_source} ${CMAKE_CURRENT_SOURCE_DIR}/run_transport_regression.py
  VERBATIM)
foreach(backend IN ITEMS Wolf Fallback)
  set(target HttpDownloader${backend}TransportTest)
  add_executable(${target} transport_fixture.cpp ${transport_include})
  target_include_directories(${target} BEFORE PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${REPO_ROOT}/test/secure_http/stubs
    ${REPO_ROOT}/freeink-sdk/libs/network/SecureNet/include)
  target_compile_definitions(${target} PRIVATE SIMULATOR)
  if(backend STREQUAL "Wolf")
    target_compile_definitions(${target} PRIVATE FREEINK_NET_WOLFSSL=1)
  endif()
  target_link_libraries(${target} PRIVATE crosspoint_test_common GTest::gtest_main)
  gtest_discover_tests(${target} TEST_PREFIX ${backend}.)
endforeach()
