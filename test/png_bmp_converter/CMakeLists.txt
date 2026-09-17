add_executable(PngBmpAllocationTest
  PngBmpAllocationTest.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/hal/HalMemory.cpp
)
target_include_directories(PngBmpAllocationTest PRIVATE
  stubs
  ${REPO_ROOT}/test/host_stubs
  ${REPO_ROOT}/test/inflate_stream/stubs
  ${REPO_ROOT}/lib/PngToBmpConverter
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/miniz/src
  ${REPO_ROOT}/lib/hal
)
add_test(NAME PngBmpAllocationFailures COMMAND PngBmpAllocationTest)
