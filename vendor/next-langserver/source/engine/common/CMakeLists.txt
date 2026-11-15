add_library(lx_common STATIC
  alloc.cpp
)

target_include_directories(lx_common PUBLIC
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(lx_common PUBLIC lx_abi)

if(LX_TESTS)
  add_executable(lx_common_test common_test.cpp)
  target_link_libraries(lx_common_test PRIVATE lx_common)
  add_test(NAME lx_common_test COMMAND lx_common_test)
  set_tests_properties(lx_common_test PROPERTIES TIMEOUT 15)
endif()
