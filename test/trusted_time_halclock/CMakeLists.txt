find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME TrustedTimeHalClock COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py)
