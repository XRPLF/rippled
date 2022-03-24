#[===================================================================[
   NIH dep: snappy
#]===================================================================]

add_library (snappy_lib STATIC IMPORTED GLOBAL)

if (NOT WIN32)
  find_package(snappy)
endif()

if(snappy)
  set_target_properties (snappy_lib PROPERTIES
    IMPORTED_LOCATION_DEBUG
      ${snappy}
    IMPORTED_LOCATION_RELEASE
      ${snappy}
    INTERFACE_INCLUDE_DIRECTORIES
    ${SNAPPY_INCLUDE_DIR})

else()
  ExternalProject_Add (snappy
    PREFIX ${nih_cache_path}
    GIT_REPOSITORY https://github.com/google/snappy.git
    GIT_TAG 1.1.7
    CMAKE_ARGS
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
      -DCMAKE_DEBUG_POSTFIX=_d
      $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
      -DBUILD_SHARED_LIBS=OFF
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
      -DSNAPPY_BUILD_TESTS=OFF
      $<$<BOOL:${MSVC}>:
        "-DCMAKE_CXX_FLAGS=-GR -Gd -fp:precise -FS -EHa -MP"
        "-DCMAKE_CXX_FLAGS_DEBUG=-MTd"
        "-DCMAKE_CXX_FLAGS_RELEASE=-MT"
      >
    LOG_BUILD ON
    LOG_CONFIGURE ON
    BUILD_COMMAND
      ${CMAKE_COMMAND}
      --build .
      --config $<CONFIG>
      --parallel ${ep_procs}
      $<$<BOOL:${is_multiconfig}>:
        COMMAND
          ${CMAKE_COMMAND} -E copy
          <BINARY_DIR>/$<CONFIG>/${ep_lib_prefix}snappy$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
          <BINARY_DIR>
        >
    TEST_COMMAND ""
    INSTALL_COMMAND
      ${CMAKE_COMMAND} -E copy_if_different <BINARY_DIR>/config.h <BINARY_DIR>/snappy-stubs-public.h <SOURCE_DIR>
    BUILD_BYPRODUCTS
      <BINARY_DIR>/${ep_lib_prefix}snappy${ep_lib_suffix}
      <BINARY_DIR>/${ep_lib_prefix}snappy_d${ep_lib_suffix}
  )
  ExternalProject_Get_Property (snappy BINARY_DIR)
  ExternalProject_Get_Property (snappy SOURCE_DIR)
  if (CMAKE_VERBOSE_MAKEFILE)
    print_ep_logs (snappy)
  endif ()
  file (MAKE_DIRECTORY ${SOURCE_DIR}/snappy)
  set_target_properties (snappy_lib PROPERTIES
    IMPORTED_LOCATION_DEBUG
      ${BINARY_DIR}/${ep_lib_prefix}snappy_d${ep_lib_suffix}
    IMPORTED_LOCATION_RELEASE
      ${BINARY_DIR}/${ep_lib_prefix}snappy${ep_lib_suffix}
    INTERFACE_INCLUDE_DIRECTORIES
      ${SOURCE_DIR})
endif()

add_dependencies (snappy_lib snappy)
target_link_libraries (ripple_libs INTERFACE snappy_lib)
exclude_if_included (snappy)
exclude_if_included (snappy_lib)
