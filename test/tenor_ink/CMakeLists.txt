add_executable(TenorInkPolicyTest PolicyTest.cpp)
target_include_directories(TenorInkPolicyTest PRIVATE ${REPO_ROOT}/lib/JpegToBmpConverter)
target_compile_options(TenorInkPolicyTest PRIVATE -UNDEBUG)
add_test(NAME TenorInkPolicy COMMAND TenorInkPolicyTest)
add_executable(TenorInkKernel Kernel.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/TenorInk.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/JpegToBmpConverter.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/GrayThumb.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/.pio/libdeps/gh_release/JPEGDEC/src/JPEGDEC.cpp)
target_include_directories(TenorInkKernel PRIVATE
  ${REPO_ROOT}/test/cover_jpeg_decode/stubs ${REPO_ROOT}/test/host_stubs
  ${REPO_ROOT}/lib/JpegToBmpConverter ${REPO_ROOT}/lib/GfxRenderer ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/.pio/libdeps/gh_release/JPEGDEC/src)
add_executable(TenorInkX3Test X3Test.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/TenorInk.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/JpegToBmpConverter.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/GrayThumb.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/.pio/libdeps/gh_release/JPEGDEC/src/JPEGDEC.cpp)
target_include_directories(TenorInkX3Test PRIVATE
  ${REPO_ROOT}/test/cover_jpeg_decode/stubs ${REPO_ROOT}/test/host_stubs
  ${REPO_ROOT}/lib/JpegToBmpConverter ${REPO_ROOT}/lib/GfxRenderer ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/.pio/libdeps/gh_release/JPEGDEC/src)
target_compile_definitions(TenorInkX3Test PRIVATE ESP_PLATFORM=1 FIXTURES="${REPO_ROOT}/test/progressive_jpeg/fixtures")
target_compile_options(TenorInkX3Test PRIVATE -UNDEBUG)
add_test(NAME TenorInkX3 COMMAND TenorInkX3Test)
