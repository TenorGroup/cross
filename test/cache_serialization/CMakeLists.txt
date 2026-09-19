# Compile the production image codec with render-only test boundaries. Keeping
# the tail verbatim avoids bringing image decoders/framebuffers into this suite.
set(IMAGE_BLOCK_FILE "${REPO_ROOT}/lib/Epub/Epub/blocks/ImageBlock.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${IMAGE_BLOCK_FILE}")
file(READ "${IMAGE_BLOCK_FILE}" IMAGE_BLOCK_SOURCE)
string(FIND "${IMAGE_BLOCK_SOURCE}" "bool ImageBlock::serialize(" IMAGE_CODEC_START)
if(IMAGE_CODEC_START LESS 0)
  message(FATAL_ERROR "ImageBlock codec entry point missing")
endif()
string(SUBSTRING "${IMAGE_BLOCK_SOURCE}" ${IMAGE_CODEC_START} -1 IMAGE_CODEC_SOURCE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ImageBlockCodec.cpp"
  "#include <Epub/blocks/ImageBlock.h>\n#include <HalMemory.h>\n#include <Serialization.h>\n${IMAGE_CODEC_SOURCE}")
add_executable(CacheSerializationTest
  CacheSerializationTest.cpp
  ImageLinkStubs.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/ImageBlockCodec.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(CacheSerializationTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib ${REPO_ROOT}/lib/Epub ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/lib/MiniBidi
)
target_link_libraries(CacheSerializationTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(CacheSerializationTest)
