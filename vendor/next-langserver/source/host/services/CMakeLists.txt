add_library(lx_host_services STATIC
  query_parser.cpp
  query_engine.cpp
  services.cpp
)
add_library(lx::host_services ALIAS lx_host_services)

target_compile_features(lx_host_services PUBLIC cxx_std_20)

target_include_directories(lx_host_services PUBLIC
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(lx_host_services PUBLIC
  lx_abi
  lx_common
  lx_atn
  lx_rope
  lx_uast
  lx_lex
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(lx_host_services PRIVATE -Wall -Wextra -Wpedantic)
endif()
