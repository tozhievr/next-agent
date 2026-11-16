add_library(lx_atn STATIC
  atn.cpp
)

target_include_directories(lx_atn PUBLIC
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(lx_atn PUBLIC lx_common lx_abi)

if(LX_TESTS)
  add_executable(lx_atn_test atn_test.cpp)
  target_link_libraries(lx_atn_test PRIVATE lx_atn)
  add_test(NAME lx_atn_test COMMAND lx_atn_test)
  set_tests_properties(lx_atn_test PROPERTIES TIMEOUT 15)
endif()
