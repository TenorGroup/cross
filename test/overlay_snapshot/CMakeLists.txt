find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME OverlaySnapshot.CacheRelease
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_cache_release.py")
add_test(NAME OverlaySnapshot.PreviewLifetime
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_preview_lifetime.py")
add_test(NAME OverlaySnapshot.BwBuffer
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_bw_buffer.py"
          --out "${CMAKE_CURRENT_BINARY_DIR}/bw-buffer-evidence")
set_tests_properties(OverlaySnapshot.CacheRelease OverlaySnapshot.PreviewLifetime OverlaySnapshot.BwBuffer
  PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
