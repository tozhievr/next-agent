add_library(lx_host_lsp STATIC
  lsp_transport.cpp
  semantic_tokens_delta.cpp
  lsp_server.cpp
)
add_library(lx::host_lsp ALIAS lx_host_lsp)

target_compile_features(lx_host_lsp PUBLIC cxx_std_20)

target_include_directories(lx_host_lsp PUBLIC
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(lx_host_lsp PUBLIC lx_host)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(lx_host_lsp PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(lxserver
  main.cpp
)
target_link_libraries(lxserver PRIVATE lx_host_lsp)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(lxserver PRIVATE -Wall -Wextra -Wpedantic)
endif()
