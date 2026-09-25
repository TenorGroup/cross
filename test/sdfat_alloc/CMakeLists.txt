# SdFat's FAT cluster search, as the firmware builds it: the libdep source with
# scripts/patch_sdfat.py applied, over a sparse in-memory card. Needs a PlatformIO
# build to have fetched SdFat into .pio/libdeps; skipped otherwise.
file(GLOB SDFAT_PARTITION_SOURCES "${REPO_ROOT}/.pio/libdeps/*/SdFat/src/FatLib/FatPartition.cpp")
option(SDFAT_ALLOC_UNPATCHED "Build the SdFat allocation test against the unpatched library" OFF)
if(SDFAT_PARTITION_SOURCES)
  list(GET SDFAT_PARTITION_SOURCES 0 SDFAT_PARTITION_SOURCE)
  get_filename_component(SDFAT_SRC "${SDFAT_PARTITION_SOURCE}" DIRECTORY)
  get_filename_component(SDFAT_SRC "${SDFAT_SRC}" DIRECTORY)
  set(SDFAT_COPY "${CMAKE_CURRENT_BINARY_DIR}/sdfat")
  file(REMOVE_RECURSE "${SDFAT_COPY}")
  file(COPY "${SDFAT_SRC}/" DESTINATION "${SDFAT_COPY}")
  if(NOT SDFAT_ALLOC_UNPATCHED)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    execute_process(COMMAND ${Python3_EXECUTABLE} "${REPO_ROOT}/scripts/patch_sdfat.py"
                            "${SDFAT_COPY}/FatLib/FatPartition.cpp" "${SDFAT_COPY}/FatLib/FatPartition.h"
                    RESULT_VARIABLE SDFAT_PATCHED)
    if(NOT SDFAT_PATCHED EQUAL 0)
      message(FATAL_ERROR "scripts/patch_sdfat.py failed on ${SDFAT_COPY}")
    endif()
  endif()
  file(GLOB SDFAT_LIB_SOURCES "${SDFAT_COPY}/FatLib/*.cpp" "${SDFAT_COPY}/common/*.cpp")
  add_executable(SdFatAllocTest SdFatAllocTest.cpp ${SDFAT_LIB_SOURCES})
  target_include_directories(SdFatAllocTest PRIVATE "${SDFAT_COPY}")
  target_compile_definitions(SdFatAllocTest PRIVATE ENABLE_ARDUINO_FEATURES=0 ENABLE_ARDUINO_SERIAL=0
                             ENABLE_ARDUINO_STRING=0 USE_BLOCK_DEVICE_INTERFACE=1 SPI_DRIVER_SELECT=3)
  target_compile_options(SdFatAllocTest PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/host_prelude.h" -w)
  target_link_libraries(SdFatAllocTest PRIVATE GTest::gtest_main)
  gtest_discover_tests(SdFatAllocTest)
else()
  message(STATUS "sdfat_alloc: no SdFat in .pio/libdeps, test skipped")
endif()
