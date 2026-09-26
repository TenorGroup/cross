# The page image converter against a decoder stand-in that serves a gray image in MCU-shaped blocks.
add_executable(ImageDownscaleTest
  ImageDownscaleTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/JpegToFramebufferConverter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/GrayThumb.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
)
target_include_directories(ImageDownscaleTest BEFORE PRIVATE stubs)
target_include_directories(ImageDownscaleTest PRIVATE
  ${REPO_ROOT}/test/host_stubs
  ${REPO_ROOT}/lib/Epub/Epub/converters
  ${REPO_ROOT}/lib/JpegToBmpConverter
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory
)
target_link_libraries(ImageDownscaleTest PRIVATE GTest::gtest_main)
gtest_discover_tests(ImageDownscaleTest)
