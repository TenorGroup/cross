add_test(
  NAME FileBackNavigation.Contract
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_contract.py ${REPO_ROOT}
)

# The simulator journeys are run by the lane's image harness. When a simulator
# executable is supplied, CTest can run the same X3 and X4 Pro journeys too.
set(FILE_BACK_SIMULATOR_X3 "" CACHE FILEPATH "simulator_x3_uc8279 executable")
set(FILE_BACK_SIMULATOR_X4PRO "" CACHE FILEPATH "simulator_x4pro executable")
if(FILE_BACK_SIMULATOR_X3)
  add_test(NAME FileBackNavigation.X3
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_file_back_keeps_selection.py
                   ${FILE_BACK_SIMULATOR_X3} ${CMAKE_CURRENT_BINARY_DIR}/x3)
endif()
if(FILE_BACK_SIMULATOR_X4PRO)
  add_test(NAME FileBackNavigation.X4Pro
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_file_back_keeps_selection_x4pro.py
                   ${FILE_BACK_SIMULATOR_X4PRO} ${CMAKE_CURRENT_BINARY_DIR}/x4pro)
endif()
