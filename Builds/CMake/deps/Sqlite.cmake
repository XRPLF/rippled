#[===================================================================[
   NIH dep: sqlite
#]===================================================================]

add_library (sqlite STATIC IMPORTED GLOBAL)

if (NOT WIN32)
  find_package(sqlite)
endif()


if(sqlite3)
  set_target_properties (sqlite PROPERTIES
    IMPORTED_LOCATION_DEBUG
      ${sqlite3}
    IMPORTED_LOCATION_RELEASE
      ${sqlite3}
    INTERFACE_INCLUDE_DIRECTORIES
    ${SQLITE_INCLUDE_DIR})

else()
  ExternalProject_Add (sqlite3
    PREFIX ${nih_cache_path}
    # sqlite doesn't use git, but it provides versioned tarballs
    URL https://www.sqlite.org/2018/sqlite-amalgamation-3260000.zip
    # ^^^ version is apparent in the URL:  3260000 => 3.26.0
    URL_HASH SHA256=de5dcab133aa339a4cf9e97c40aa6062570086d6085d8f9ad7bc6ddf8a52096e
    # we wrote a very simple CMake file to build sqlite
    # so that's what we copy here so that we can build with
    # CMake. sqlite doesn't generally provided a build system
    # for the single amalgamation source file.
    PATCH_COMMAND
      ${CMAKE_COMMAND} -E copy_if_different
      ${CMAKE_CURRENT_SOURCE_DIR}/Builds/CMake/CMake_sqlite3.txt
      <SOURCE_DIR>/CMakeLists.txt
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
      $<$<BOOL:${is_multiconfig}>:
        COMMAND
          ${CMAKE_COMMAND} -E copy
            <BINARY_DIR>/$<CONFIG>/${ep_lib_prefix}sqlite3$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
            <BINARY_DIR>
        >
    TEST_COMMAND ""
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS
      <BINARY_DIR>/${ep_lib_prefix}sqlite3${ep_lib_suffix}
      <BINARY_DIR>/${ep_lib_prefix}sqlite3_d${ep_lib_suffix}
  )
  ExternalProject_Get_Property (sqlite3 BINARY_DIR)
  ExternalProject_Get_Property (sqlite3 SOURCE_DIR)
  if (CMAKE_VERBOSE_MAKEFILE)
    print_ep_logs (sqlite3)
  endif ()

  set_target_properties (sqlite PROPERTIES
    IMPORTED_LOCATION_DEBUG
      ${BINARY_DIR}/${ep_lib_prefix}sqlite3_d${ep_lib_suffix}
    IMPORTED_LOCATION_RELEASE
      ${BINARY_DIR}/${ep_lib_prefix}sqlite3${ep_lib_suffix}
    INTERFACE_INCLUDE_DIRECTORIES
      ${SOURCE_DIR})

  add_dependencies (sqlite sqlite3)
  exclude_if_included (sqlite3)
endif()

target_link_libraries (sqlite INTERFACE $<$<NOT:$<BOOL:${MSVC}>>:dl>)
target_link_libraries (ripple_libs INTERFACE sqlite)
exclude_if_included (sqlite)
set(sqlite_BINARY_DIR ${BINARY_DIR})
