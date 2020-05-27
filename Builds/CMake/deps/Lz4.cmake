#[===================================================================[
   NIH dep: lz4
#]===================================================================]

add_library (lz4_lib STATIC IMPORTED GLOBAL)

if (NOT WIN32)
  find_package(lz4)
endif()

if(lz4)
  set_target_properties (lz4_lib PROPERTIES
    IMPORTED_LOCATION_DEBUG
      ${lz4}
    IMPORTED_LOCATION_RELEASE
      ${lz4}
    INTERFACE_INCLUDE_DIRECTORIES
      ${LZ4_INCLUDE_DIR})

else()
  ExternalProject_Add (lz4
    PREFIX ${nih_cache_path}
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    GIT_TAG v1.9.2
    SOURCE_SUBDIR contrib/cmake_unofficial
    CMAKE_ARGS
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
      -DCMAKE_DEBUG_POSTFIX=_d
      $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
      -DBUILD_STATIC_LIBS=ON
      -DBUILD_SHARED_LIBS=OFF
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
      --target lz4_static
      $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel ${ep_procs}>
      $<$<BOOL:${is_multiconfig}>:
        COMMAND
          ${CMAKE_COMMAND} -E copy
          <BINARY_DIR>/$<CONFIG>/${ep_lib_prefix}lz4$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
          <BINARY_DIR>
        >
    TEST_COMMAND ""
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS
      <BINARY_DIR>/${ep_lib_prefix}lz4${ep_lib_suffix}
      <BINARY_DIR>/${ep_lib_prefix}lz4_d${ep_lib_suffix}
  )
  ExternalProject_Get_Property (lz4 BINARY_DIR)
  ExternalProject_Get_Property (lz4 SOURCE_DIR)

  file (MAKE_DIRECTORY ${SOURCE_DIR}/lz4)
  set_target_properties (lz4_lib PROPERTIES
    IMPORTED_LOCATION_DEBUG
      ${BINARY_DIR}/${ep_lib_prefix}lz4_d${ep_lib_suffix}
    IMPORTED_LOCATION_RELEASE
      ${BINARY_DIR}/${ep_lib_prefix}lz4${ep_lib_suffix}
    INTERFACE_INCLUDE_DIRECTORIES
      ${SOURCE_DIR}/lib)

  if (CMAKE_VERBOSE_MAKEFILE)
    print_ep_logs (lz4)
  endif ()
endif()

add_dependencies (lz4_lib lz4)
target_link_libraries (ripple_libs INTERFACE lz4_lib)
exclude_if_included (lz4)
exclude_if_included (lz4_lib)
