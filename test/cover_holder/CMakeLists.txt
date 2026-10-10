# The cover placeholder: the master inflates strip by strip, the tenor/ink and 1 bit paths write it.
# test_cover_holder.py compares the BMPs with test/cover_holder/reference.py, written from the definition.
add_executable(CoverHolderTool Tool.cpp
  ${REPO_ROOT}/src/components/CoverHolder.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/TenorInk.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/GrayThumb.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/hal/HalMemory.cpp)
target_include_directories(CoverHolderTool PRIVATE
  ${REPO_ROOT}/test/cover_jpeg_decode/stubs ${REPO_ROOT}/test/host_stubs ${REPO_ROOT}/test/inflate_stream/stubs
  ${REPO_ROOT}/src ${REPO_ROOT}/lib/JpegToBmpConverter ${REPO_ROOT}/lib/GfxRenderer ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/hal ${REPO_ROOT}/lib/miniz/src ${REPO_ROOT}/lib/Utf8)
add_test(NAME CoverHolder COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_cover_holder.py"
  "$<TARGET_FILE:CoverHolderTool>")
