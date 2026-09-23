find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME X4SleepClockProductionPaths
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
    --output ${CMAKE_CURRENT_BINARY_DIR}/harness --cxx ${CMAKE_CXX_COMPILER})
add_test(NAME X4SleepClockMainWiring
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/wiring.py --cxx ${CMAKE_CXX_COMPILER})
add_test(NAME X3StoredWakeButtonsSleepLikePowerOnly
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/journey.py
    --output ${CMAKE_CURRENT_BINARY_DIR}/journey --cxx ${CMAKE_CXX_COMPILER})
