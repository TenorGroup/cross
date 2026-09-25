# The cover decodes on the real JPEGDEC (.pio/libdeps/gh_release, populated by `pio run -e gh_release`).
set(JPEGDEC_SRC "${REPO_ROOT}/.pio/libdeps/gh_release/JPEGDEC/src")
add_executable(CoverJpegDecodeTest
  CoverJpegDecodeTest.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/JpegToBmpConverter.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/GrayThumb.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${JPEGDEC_SRC}/JPEGDEC.cpp
)
target_include_directories(CoverJpegDecodeTest PRIVATE
  stubs
  ${REPO_ROOT}/test/host_stubs
  ${REPO_ROOT}/lib/JpegToBmpConverter
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory
  ${JPEGDEC_SRC}
)
target_compile_definitions(CoverJpegDecodeTest PRIVATE FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME CoverJpegDecode COMMAND CoverJpegDecodeTest)
