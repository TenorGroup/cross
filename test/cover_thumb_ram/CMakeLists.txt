add_executable(CoverThumbRamTest
  CoverThumbRamTest.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/GrayThumb.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
)
target_include_directories(CoverThumbRamTest PRIVATE
  stubs
  ${REPO_ROOT}/test/host_stubs
  ${REPO_ROOT}/test/png_bmp_converter/stubs
  ${REPO_ROOT}/lib/JpegToBmpConverter
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/src
)
add_test(NAME CoverThumbRam COMMAND CoverThumbRamTest)
add_executable(GrayThumbFailureTest
  GrayThumbFailureTest.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/GrayThumb.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
)
get_target_property(thumb_includes CoverThumbRamTest INCLUDE_DIRECTORIES)
target_include_directories(GrayThumbFailureTest PRIVATE ${thumb_includes})
target_compile_options(GrayThumbFailureTest PRIVATE -UNDEBUG)
add_test(NAME GrayThumbFailure COMMAND GrayThumbFailureTest)
