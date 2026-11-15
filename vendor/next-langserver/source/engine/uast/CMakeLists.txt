add_library(lx_uast STATIC
  uast.cpp
  seq.cpp
  cursor.cpp
  diff.cpp
)

target_include_directories(lx_uast PUBLIC
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(lx_uast PRIVATE -Wall -Wextra -Wpedantic)
endif()
