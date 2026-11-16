cmake_minimum_required(VERSION 3.20)
project(next_langserver LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LX_TESTS "Build unit and integration tests" ON)

if(LX_TESTS)
  set(CTEST_TEST_TIMEOUT 15)
  set(DART_TESTING_TIMEOUT 15)
  enable_testing()
endif()

add_library(lx_abi INTERFACE)
target_include_directories(lx_abi INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

# Helper macro to add a subdirectory if its CMakeLists.txt exists, tracking added paths.
set(_lx_added_subdirs "")
macro(lx_add_subdir_if_exists rel_path)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${rel_path}/CMakeLists.txt")
    list(FIND _lx_added_subdirs "${rel_path}" _lx_found)
    if(_lx_found EQUAL -1)
      list(APPEND _lx_added_subdirs "${rel_path}")
      add_subdirectory("${rel_path}")
    endif()
  endif()
endmacro()

# Core engine libraries in dependency order
lx_add_subdir_if_exists("engine/common")
lx_add_subdir_if_exists("engine/atn")
lx_add_subdir_if_exists("engine/rope")
lx_add_subdir_if_exists("engine/uast")
lx_add_subdir_if_exists("engine/lex")
lx_add_subdir_if_exists("engine/parse")

# Any additional engine/* subdirectories
file(GLOB _lx_engine_dirs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "engine/*")
list(SORT _lx_engine_dirs)
foreach(_dir IN LISTS _lx_engine_dirs)
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}")
    lx_add_subdir_if_exists("${_dir}")
  endif()
endforeach()

# Host and LSP server
lx_add_subdir_if_exists("host")

# Tests
if(LX_TESTS)
  lx_add_subdir_if_exists("tests/rope")
  lx_add_subdir_if_exists("tests/uast")
  lx_add_subdir_if_exists("tests/lex")
  lx_add_subdir_if_exists("tests/parse")
  lx_add_subdir_if_exists("tests/host")
  lx_add_subdir_if_exists("tests/integration")

  file(GLOB _lx_test_dirs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "tests/*")
  list(SORT _lx_test_dirs)
  foreach(_dir IN LISTS _lx_test_dirs)
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}")
      lx_add_subdir_if_exists("${_dir}")
    endif()
  endforeach()
endif()
