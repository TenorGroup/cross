add_test(NAME ImageProbeLoan.parser
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
          "${REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/parser")
add_test(NAME ImageProbeLoan.renderer
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/renderer.py"
          "${REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/renderer")
add_test(NAME ImageProbeLoan.reader_frame
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/reader.py"
          "${REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/reader")
add_test(NAME ImageProbeLoan.repaint_status_usb
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/repaint_status_usb.py"
          "${REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/repaint_status_usb")

set_tests_properties(ImageProbeLoan.parser ImageProbeLoan.renderer ImageProbeLoan.reader_frame
  ImageProbeLoan.repaint_status_usb
  PROPERTIES TIMEOUT 60)
