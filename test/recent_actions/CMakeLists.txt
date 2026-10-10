add_test(NAME RecentActionsContract
         COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_contract.py" "${REPO_ROOT}")

add_test(NAME RecentContextMenu COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_menu.py" --repo "${REPO_ROOT}" --output "${CMAKE_CURRENT_BINARY_DIR}/menu")
