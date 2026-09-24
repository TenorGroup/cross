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
)
add_test(NAME CoverThumbRam COMMAND CoverThumbRamTest)
