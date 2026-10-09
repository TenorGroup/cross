add_test(NAME TextGrayPolicyProductionBehavior
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
          --repo "${REPO_ROOT}" --out "${CMAKE_CURRENT_BINARY_DIR}/text-gray-policy")
