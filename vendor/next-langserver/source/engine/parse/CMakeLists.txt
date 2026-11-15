add_library(lx_parse STATIC
  diagnostics.cpp
  dfa_cache.cpp
  parser.cpp
)
add_library(lx::parse ALIAS lx_parse)

target_compile_features(lx_parse PUBLIC cxx_std_20)

target_include_directories(lx_parse PUBLIC
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/engine"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(lx_parse PUBLIC
  lx_abi
  lx_common
  lx_atn
  lx_rope
  lx_uast
  lx_lex
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(lx_parse PRIVATE -Wall -Wextra -Wpedantic)
endif()
