# The simulator journeys are run when a lane supplies the corresponding
# executable. Each assertion compares against a fixed card or an earlier
# screenshot from the same journey.
add_test(
  NAME FileBackNavigation.Contract
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_contract.py ${REPO_ROOT}
)

set(FILE_BACK_SIMULATOR_X3 "" CACHE FILEPATH "simulator_x3_uc8279 executable")
set(FILE_BACK_SIMULATOR_X4C "" CACHE FILEPATH "simulator_x4c_n6 executable")
set(FILE_BACK_SIMULATOR_X4PRO "" CACHE FILEPATH "simulator_x4pro executable")
function(add_file_journeys machine binary)
  foreach(case favorite reopen sibling direct)
    add_test(NAME FileBackNavigation.${machine}.${case}
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/journeys.py
                     ${binary} ${CMAKE_CURRENT_BINARY_DIR}/${machine}-${case} ${case}
                     --machine ${machine})
  endforeach()
endfunction()
if(FILE_BACK_SIMULATOR_X3)
  add_file_journeys(X3 "${FILE_BACK_SIMULATOR_X3}")
  add_test(NAME FileBackNavigation.X3Selection
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_file_back_keeps_selection.py
                   ${FILE_BACK_SIMULATOR_X3} ${CMAKE_CURRENT_BINARY_DIR}/x3)
  add_test(NAME FileBackNavigation.X3HomeDirectChild
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_home_file_keeps_selection.py
                   ${FILE_BACK_SIMULATOR_X3} ${CMAKE_CURRENT_BINARY_DIR}/x3-home)
endif()
if(FILE_BACK_SIMULATOR_X4C)
  add_file_journeys(X4C "${FILE_BACK_SIMULATOR_X4C}")
endif()
if(FILE_BACK_SIMULATOR_X4PRO)
  add_test(NAME FileBackNavigation.X4ProFavorite
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/journeys.py
                   ${FILE_BACK_SIMULATOR_X4PRO} ${CMAKE_CURRENT_BINARY_DIR}/X4Pro-favorite favorite
                   --machine X4Pro)
  add_test(NAME FileBackNavigation.X4ProReopen
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/journeys.py
                   ${FILE_BACK_SIMULATOR_X4PRO} ${CMAKE_CURRENT_BINARY_DIR}/X4Pro-reopen reopen
                   --machine X4Pro)
  add_test(NAME FileBackNavigation.X4ProMidList
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_file_back_keeps_selection_x4pro.py
                   ${FILE_BACK_SIMULATOR_X4PRO} ${CMAKE_CURRENT_BINARY_DIR}/x4pro-mid-list)
endif()
