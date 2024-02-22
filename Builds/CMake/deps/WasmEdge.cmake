#[===================================================================[
   NIH dep: wasmedge: web assembly runtime for hooks.
#]===================================================================]

if (is_root_project) # WasmEdge not needed in the case of xrpl_core inclusion build
  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(OLD_DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
    set(CMAKE_DEBUG_POSTFIX _d)
  endif ()
  add_library (wasmedge INTERFACE)
  ExternalProject_Add (wasmedge_src
    PREFIX ${nih_cache_path}
    GIT_REPOSITORY https://github.com/WasmEdge/WasmEdge.git
    GIT_TAG 0.9.0
      CMAKE_ARGS
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
    -DCMAKE_DEBUG_POSTFIX=_d
    $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
    $<$<BOOL:${MSVC}>:
      "-DCMAKE_C_FLAGS=-GR -Gd -fp:precise -FS -MP"
      "-DCMAKE_C_FLAGS_DEBUG=-MTd"
      "-DCMAKE_C_FLAGS_RELEASE=-MT"
    >
    LOG_BUILD ON
    LOG_CONFIGURE ON
    BUILD_COMMAND
      ${CMAKE_COMMAND}
      --build .
      --config $<CONFIG>
      $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel ${ep_procs}>
    TEST_COMMAND ""
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS
      <BINARY_DIR>/lib/api/libwasmedge_c_${CMAKE_DEBUG_POSTFIX}.so
  )
  ExternalProject_Get_Property (wasmedge_src BINARY_DIR)
  set (wasmedge_src_BINARY_DIR "${BINARY_DIR}")
  add_dependencies (wasmedge wasmedge_src)
  target_include_directories (ripple_libs SYSTEM INTERFACE "${wasmedge_src_BINARY_DIR}/include/api")
  target_link_libraries (wasmedge
    INTERFACE
      Boost::thread
      Boost::system)
  target_link_libraries (wasmedge INTERFACE
      "${wasmedge_src_BINARY_DIR}/lib/api/libwasmedge_c${CMAKE_DEBUG_POSTFIX}.so")
  target_link_libraries (ripple_libs INTERFACE wasmedge)
  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_DEBUG_POSTFIX ${OLD_DEBUG_POSTFIX})
  endif ()
endif ()
