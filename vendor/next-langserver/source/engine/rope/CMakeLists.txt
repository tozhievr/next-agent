add_library(lx_rope STATIC
  rope.cpp
)
add_library(lx::rope ALIAS lx_rope)

target_compile_features(lx_rope PUBLIC cxx_std_20)

target_include_directories(lx_rope PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/../.."
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)

if(TARGET lx_abi)
  target_link_libraries(lx_rope PUBLIC lx_abi)
endif()
