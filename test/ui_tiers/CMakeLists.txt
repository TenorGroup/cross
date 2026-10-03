cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(ui_tier_tests C CXX)
  enable_testing()
  set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endif()
add_executable(UITextSizeSpecTest UIScaleTest.cpp)
target_compile_features(UITextSizeSpecTest PRIVATE cxx_std_17)
target_include_directories(UITextSizeSpecTest PRIVATE ${REPO_ROOT}/src)
add_test(NAME UITextSizeSpec COMMAND UITextSizeSpecTest)
add_executable(UIExtraSizeLifetimeTest ExtraSizeLifetimeTest.cpp ${REPO_ROOT}/lib/EpdFont/SdCardFontManager.cpp)
target_compile_features(UIExtraSizeLifetimeTest PRIVATE cxx_std_17)
target_include_directories(UIExtraSizeLifetimeTest PRIVATE manager-stubs stubs ${REPO_ROOT}/lib/EpdFont)
add_test(NAME UIExtraSizeLifetime COMMAND UIExtraSizeLifetimeTest)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME UIFaces COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_ui_faces.py)
add_test(NAME UIBootAndApply
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_boot_apply.py
          --build ${CMAKE_CURRENT_BINARY_DIR}/boot-apply)

# The root host suite supplies -DNDEBUG even with an empty build type.
# Keep assertions in any production code linked into these tests enabled too.
foreach(target UITextSizeSpecTest UIExtraSizeLifetimeTest)
  if(MSVC)
    target_compile_options(${target} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
endforeach()
