# Behavioral regressions compile production runtime and extracted integration
# functions against deterministic scheduler, input and storage boundaries.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_subdirectory(lifecycle)
add_subdirectory(reader)
add_test(NAME S3BleProfiles COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_s3_profiles.py)
