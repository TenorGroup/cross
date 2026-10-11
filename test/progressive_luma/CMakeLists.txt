add_executable(ProgressiveLumaHost LumaHost.cpp ${REPO_ROOT}/lib/JpegToBmpConverter/ProgressiveLuma.cpp)
target_include_directories(ProgressiveLumaHost PRIVATE
  ${REPO_ROOT}/test/cover_jpeg_decode/stubs ${REPO_ROOT}/test/host_stubs
  ${REPO_ROOT}/lib/JpegToBmpConverter ${REPO_ROOT}/lib/Memory)
target_compile_definitions(ProgressiveLumaHost PRIVATE ESP_PLATFORM=1)
target_compile_options(ProgressiveLumaHost PRIVATE -UNDEBUG -Werror -fsanitize=address,undefined -fno-sanitize-recover=all)
target_link_options(ProgressiveLumaHost PRIVATE -fsanitize=address,undefined)
add_test(NAME ProgressiveLuma COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py $<TARGET_FILE:ProgressiveLumaHost>)
