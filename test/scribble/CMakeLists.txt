# Scribbled gestures (strike, ring, tap, swipe) on the generated strokes in samples.txt and on any
# strokes drawn on a device (samples-*.txt, from log_to_samples.py).
add_executable(ScribbleTest ScribbleTest.cpp)
target_link_libraries(ScribbleTest PRIVATE crosspoint_test_common GTest::gtest_main)
target_compile_options(ScribbleTest PRIVATE -Werror)
target_compile_definitions(ScribbleTest PRIVATE SCRIBBLE_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
gtest_discover_tests(ScribbleTest)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME ScribbleLogToSamples COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/log_to_samples.py --selftest)
