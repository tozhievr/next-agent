add_library(lx_lex STATIC
  state_interner.cpp
  lexdrv.cpp
)
add_library(lx::lex ALIAS lx_lex)

target_compile_features(lx_lex PUBLIC cxx_std_20)

target_include_directories(lx_lex PUBLIC
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/engine"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(lx_lex PUBLIC
  lx_abi
  lx_rope
  lx_uast
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(lx_lex PRIVATE -Wall -Wextra -Wpedantic)
endif()
