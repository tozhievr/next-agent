set(_lx_host_core_sources
  seh_guard.c
  json.cpp
  metrics.cpp
  scheduler.cpp
  uri.cpp
  module_loader.cpp
  snapshot.cpp
  parser_adapter.cpp
  layer_manager.cpp
  session.cpp
  document_store.cpp
)

set(_lx_has_services_subdir FALSE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/services/CMakeLists.txt")
  set(_lx_has_services_subdir TRUE)
endif()

set(_lx_has_lsp_subdir FALSE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lsp/CMakeLists.txt")
  set(_lx_has_lsp_subdir TRUE)
endif()

if(_lx_has_services_subdir)
  add_library(lx_host STATIC ${_lx_host_core_sources})
else()
  add_library(lx_host STATIC
    ${_lx_host_core_sources}
    query_parser.cpp
    query_engine.cpp
    services.cpp
  )
endif()
add_library(lx::host ALIAS lx_host)

target_compile_features(lx_host PUBLIC cxx_std_20)

target_include_directories(lx_host PUBLIC
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(lx_host PUBLIC
  lx_abi
  lx_common
  lx_atn
  lx_rope
  lx_uast
  lx_lex
)

if(TARGET lx_parse)
  target_link_libraries(lx_host PUBLIC lx_parse)
  target_compile_definitions(lx_host PUBLIC LX_HAS_ENGINE_PARSE=1)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(lx_host PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra>
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>
  )
endif()

if(_lx_has_services_subdir)
  add_subdirectory(services)
  if(TARGET lx_host_services)
    target_link_libraries(lx_host PUBLIC lx_host_services)
  endif()
endif()

if(_lx_has_lsp_subdir)
  add_subdirectory(lsp)
else()
  add_library(lx_host_lsp STATIC
    lsp_transport.cpp
    lsp_server.cpp
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
endif()
