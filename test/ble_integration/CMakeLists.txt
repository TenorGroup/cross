# Behavioral regressions compile production runtime and extracted integration
# functions against deterministic scheduler, input and storage boundaries.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_subdirectory(lifecycle)
add_subdirectory(reader)
