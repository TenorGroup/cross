add_executable(ProgressiveJpegTest
  ProgressiveJpegTest.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/ProgressiveJpeg.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/JpegToBmpConverter.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/TenorInk.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/GrayThumb.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/.pio/libdeps/gh_release/JPEGDEC/src/JPEGDEC.cpp)
target_include_directories(ProgressiveJpegTest PRIVATE
  ${REPO_ROOT}/test/cover_jpeg_decode/stubs ${REPO_ROOT}/test/host_stubs
  ${REPO_ROOT}/lib/JpegToBmpConverter ${REPO_ROOT}/lib/GfxRenderer ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/hal ${REPO_ROOT}/.pio/libdeps/gh_release/JPEGDEC/src)
target_compile_definitions(ProgressiveJpegTest PRIVATE BOARD_HAS_PSRAM=1 FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_compile_options(ProgressiveJpegTest PRIVATE -UNDEBUG -Wno-unused-function)
add_test(NAME ProgressiveJpeg COMMAND ProgressiveJpegTest)
