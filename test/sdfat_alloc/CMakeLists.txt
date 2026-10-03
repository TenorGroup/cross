# SdFat's FAT cluster search, as the firmware builds it: the libdep source with
# scripts/patch_sdfat.py and scripts/sdfat_patches/*.patch applied, over a sparse in-memory card.
# Built twice, with the shared FAT cache and with the separate one (USE_SEPARATE_FAT_CACHE 0|1,
# the firmware's crosspoint.separate_fat_cache); both builds run the same tests.
# Needs a PlatformIO build to have fetched SdFat into .pio/libdeps; skipped otherwise.
file(GLOB SDFAT_PARTITION_SOURCES "${REPO_ROOT}/.pio/libdeps/*/SdFat/src/FatLib/FatPartition.cpp")
option(SDFAT_ALLOC_UNPATCHED "Build the SdFat allocation test against the unpatched library" OFF)
option(SDFAT_CACHE_UNPATCHED "Leave out scripts/sdfat_patches (cache patches), to see the tests fail" OFF)
if(SDFAT_PARTITION_SOURCES)
  list(GET SDFAT_PARTITION_SOURCES 0 SDFAT_PARTITION_SOURCE)
  get_filename_component(SDFAT_SRC "${SDFAT_PARTITION_SOURCE}" DIRECTORY)
  get_filename_component(SDFAT_SRC "${SDFAT_SRC}" DIRECTORY)
  set(SDFAT_LIB_COPY "${CMAKE_CURRENT_BINARY_DIR}/sdfat-lib")
  set(SDFAT_COPY "${SDFAT_LIB_COPY}/src")
  file(REMOVE_RECURSE "${SDFAT_LIB_COPY}")
  file(COPY "${SDFAT_SRC}" DESTINATION "${SDFAT_LIB_COPY}")
  # The libdep copy is patched by the firmware build; the unpatched run takes it back to the
  # library's own text, so the tests show what the patch changes.
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(SDFAT_PATCH_ARGS "")
  if(SDFAT_ALLOC_UNPATCHED)
    set(SDFAT_PATCH_ARGS "--unpatch")
  endif()
  execute_process(COMMAND ${Python3_EXECUTABLE} "${REPO_ROOT}/scripts/patch_sdfat.py" ${SDFAT_PATCH_ARGS}
                          "${SDFAT_COPY}/FatLib/FatPartition.cpp" "${SDFAT_COPY}/FatLib/FatPartition.h"
                  RESULT_VARIABLE SDFAT_PATCHED)
  if(NOT SDFAT_PATCHED EQUAL 0)
    message(FATAL_ERROR "scripts/patch_sdfat.py failed on ${SDFAT_COPY}")
  endif()
  # The cache patches (scripts/patch_sdfat_cache.py applies the same files in the firmware build):
  # a libdep the firmware build already patched is left as it is.
  find_package(Git REQUIRED)
  file(GLOB SDFAT_CACHE_PATCHES "${REPO_ROOT}/scripts/sdfat_patches/*.patch")
  list(SORT SDFAT_CACHE_PATCHES)
  if(SDFAT_CACHE_UNPATCHED)
    set(SDFAT_CACHE_PATCHES "")
  endif()
  # git must not find this repo above the build directory, or it would resolve paths against it.
  set(SDFAT_GIT ${CMAKE_COMMAND} -E env GIT_CEILING_DIRECTORIES=${CMAKE_CURRENT_BINARY_DIR} ${GIT_EXECUTABLE})
  foreach(SDFAT_PATCH ${SDFAT_CACHE_PATCHES})
    execute_process(COMMAND ${SDFAT_GIT} apply --check --reverse "${SDFAT_PATCH}"
                    WORKING_DIRECTORY "${SDFAT_LIB_COPY}" RESULT_VARIABLE SDFAT_ALREADY OUTPUT_QUIET ERROR_QUIET)
    if(NOT SDFAT_ALREADY EQUAL 0)
      execute_process(COMMAND ${SDFAT_GIT} apply "${SDFAT_PATCH}"
                      WORKING_DIRECTORY "${SDFAT_LIB_COPY}" RESULT_VARIABLE SDFAT_APPLIED)
      if(NOT SDFAT_APPLIED EQUAL 0)
        message(FATAL_ERROR "${SDFAT_PATCH} does not apply to ${SDFAT_LIB_COPY}")
      endif()
    endif()
  endforeach()
  file(GLOB SDFAT_LIB_SOURCES "${SDFAT_COPY}/FatLib/*.cpp" "${SDFAT_COPY}/common/*.cpp")
  foreach(SEPARATE 0 1)
    set(TARGET_NAME SdFatAllocTest)
    set(TEST_PREFIX "")
    if(SEPARATE)
      set(TARGET_NAME SdFatAllocTestSeparateCache)
      set(TEST_PREFIX "SeparateFatCache.")
    endif()
    add_executable(${TARGET_NAME} SdFatAllocTest.cpp ${SDFAT_LIB_SOURCES})
    target_include_directories(${TARGET_NAME} PRIVATE "${SDFAT_COPY}"
                               "${REPO_ROOT}/freeink-sdk/libs/hardware/SDCardManager/include")
    # SDFAT_EXPECT_SEPARATE_FAT_CACHE is what the source's static_assert holds the library's own
    # USE_SEPARATE_FAT_CACHE to; the build runs with -w, so a redefined flag would pass unseen.
    target_compile_definitions(${TARGET_NAME} PRIVATE ENABLE_ARDUINO_FEATURES=0 ENABLE_ARDUINO_SERIAL=0
                               ENABLE_ARDUINO_STRING=0 USE_BLOCK_DEVICE_INTERFACE=1 SPI_DRIVER_SELECT=3
                               USE_SEPARATE_FAT_CACHE=${SEPARATE} SDFAT_EXPECT_SEPARATE_FAT_CACHE=${SEPARATE})
    target_compile_options(${TARGET_NAME} PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/host_prelude.h" -w)
    target_link_libraries(${TARGET_NAME} PRIVATE GTest::gtest_main)
    gtest_discover_tests(${TARGET_NAME} TEST_PREFIX "${TEST_PREFIX}")
  endforeach()
else()
  message(WARNING "sdfat_alloc: no SdFat in .pio/libdeps (build a firmware environment first); SdFat allocation tests skipped")
endif()
