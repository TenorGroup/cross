set(INK ${REPO_ROOT}/lib/JpegToBmpConverter)
add_executable(CoverInkTest CoverInkTest.cpp ${INK}/CoverInk.cpp ${INK}/ProgressiveLuma.cpp ${INK}/TenorInk.cpp
  ${INK}/JpegToBmpConverter.cpp ${INK}/GrayThumb.cpp ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/.pio/libdeps/gh_release/JPEGDEC/src/JPEGDEC.cpp)
target_include_directories(CoverInkTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${REPO_ROOT}/test/cover_jpeg_decode/stubs ${REPO_ROOT}/test/host_stubs
  ${INK} ${REPO_ROOT}/lib/GfxRenderer ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/IdleWork
  ${REPO_ROOT}/freeink-sdk/libs/hardware/SDCardManager/include ${REPO_ROOT}/.pio/libdeps/gh_release/JPEGDEC/src)
target_compile_definitions(CoverInkTest PRIVATE ESP_PLATFORM=1 FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_compile_options(CoverInkTest PRIVATE -UNDEBUG -fsanitize=address,undefined -fno-sanitize-recover=all)
target_link_options(CoverInkTest PRIVATE -fsanitize=address,undefined)
add_test(NAME CoverInk COMMAND CoverInkTest)
# The X3's JPEGDEC path on the same cover, outside the sanitizers (JPEGDEC itself trips them):
# it fails, so Home has no temporary cover for it.
add_executable(CoverInkJpegdecTest JpegdecTest.cpp ${INK}/JpegToBmpConverter.cpp ${INK}/TenorInk.cpp ${INK}/GrayThumb.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp ${REPO_ROOT}/.pio/libdeps/gh_release/JPEGDEC/src/JPEGDEC.cpp)
target_include_directories(CoverInkJpegdecTest PRIVATE
  ${REPO_ROOT}/test/cover_jpeg_decode/stubs ${REPO_ROOT}/test/host_stubs ${INK} ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/.pio/libdeps/gh_release/JPEGDEC/src)
target_compile_definitions(CoverInkJpegdecTest PRIVATE ESP_PLATFORM=1 FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_compile_options(CoverInkJpegdecTest PRIVATE -UNDEBUG)
add_test(NAME CoverInkJpegdec COMMAND CoverInkJpegdecTest)
