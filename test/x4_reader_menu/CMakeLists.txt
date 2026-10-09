find_package(Python3 REQUIRED COMPONENTS Interpreter)
foreach(case IN ITEMS state layout chrome)
  if(case STREQUAL "layout")
    add_test(NAME X4ReaderMenu.${case}
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/${case}.py"
        --repo "${REPO_ROOT}" --output "${CMAKE_CURRENT_BINARY_DIR}/${case}"
        --expect-preview-hit --expect-fade --expect-stepper-ring --expect-stepper-center --expect-middle-font)
  else()
    add_test(NAME X4ReaderMenu.${case}
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/${case}.py"
        --repo "${REPO_ROOT}" --output "${CMAKE_CURRENT_BINARY_DIR}/${case}")
  endif()
endforeach()
