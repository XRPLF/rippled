#[===================================================================[
   NIH dep: soci
#]===================================================================]

foreach (_comp core empty sqlite3)
  add_library ("soci_${_comp}" STATIC IMPORTED GLOBAL)
endforeach ()

if (NOT WIN32)
  find_package(soci)
endif()

if (soci)
  foreach (_comp core empty sqlite3)
    set_target_properties ("soci_${_comp}" PROPERTIES
      IMPORTED_LOCATION_DEBUG
        ${soci}
      IMPORTED_LOCATION_RELEASE
        ${soci}
      INTERFACE_INCLUDE_DIRECTORIES
        ${SOCI_INCLUDE_DIR})
  endforeach ()

else()
  set (soci_lib_pre ${ep_lib_prefix})
  set (soci_lib_post "")
  if (WIN32)
    # for some reason soci on windows still prepends lib (non-standard)
    set (soci_lib_pre lib)
    # this version in the name might change if/when we change versions of soci
    set (soci_lib_post "_4_0")
  endif ()
  get_target_property (_boost_incs Boost::date_time INTERFACE_INCLUDE_DIRECTORIES)
  get_target_property (_boost_dt Boost::date_time IMPORTED_LOCATION)
  if (NOT _boost_dt)
    get_target_property (_boost_dt Boost::date_time IMPORTED_LOCATION_RELEASE)
  endif ()
  if (NOT _boost_dt)
    get_target_property (_boost_dt Boost::date_time IMPORTED_LOCATION_DEBUG)
  endif ()

  ExternalProject_Add (soci
    PREFIX ${nih_cache_path}
    GIT_REPOSITORY https://github.com/SOCI/soci.git
    GIT_TAG 04e1870294918d20761736743bb6136314c42dd5
    # We had an issue with soci integer range checking for boost::optional
    # and needed to remove the exception that SOCI throws in this case.
    # This is *probably* a bug in SOCI, but has never been investigated more
    # nor reported to the maintainers.
    # This cmake script comments out the lines in question.
    # This patch process is likely fragile and should be reviewed carefully
    # whenever we update the GIT_TAG above.
    PATCH_COMMAND
      ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/Builds/CMake/soci_patch.cmake
    CMAKE_ARGS
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
      $<$<BOOL:${CMAKE_TOOLCHAIN_FILE}>:-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}>
      $<$<BOOL:${VCPKG_TARGET_TRIPLET}>:-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}>
      $<$<BOOL:${unity}>:-DCMAKE_UNITY_BUILD=ON}>
      -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/sqlite3
      -DCMAKE_MODULE_PATH=${CMAKE_CURRENT_SOURCE_DIR}/Builds/CMake
      -DCMAKE_INCLUDE_PATH=$<JOIN:$<TARGET_PROPERTY:sqlite,INTERFACE_INCLUDE_DIRECTORIES>,::>
      -DCMAKE_LIBRARY_PATH=${sqlite_BINARY_DIR}
      -DCMAKE_DEBUG_POSTFIX=_d
      $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
      -DSOCI_CXX_C11=ON
      -DSOCI_STATIC=ON
      -DSOCI_LIBDIR=lib
      -DSOCI_SHARED=OFF
      -DSOCI_TESTS=OFF
      # hacks to workaround the fact that soci doesn't currently use
      # boost imported targets in its cmake. If they switch to
      # proper imported targets, this next line can be removed
      # (as well as the get_property above that sets _boost_incs)
      -DBoost_INCLUDE_DIRS=$<JOIN:${_boost_incs},::>
      -DBoost_INCLUDE_DIR=$<JOIN:${_boost_incs},::>
      -DBOOST_ROOT=${BOOST_ROOT}
      -DWITH_BOOST=ON
      -DBoost_FOUND=ON
      -DBoost_NO_BOOST_CMAKE=ON
      -DBoost_DATE_TIME_FOUND=ON
      -DSOCI_HAVE_BOOST=ON
      -DSOCI_HAVE_BOOST_DATE_TIME=ON
      -DBoost_DATE_TIME_LIBRARY=${_boost_dt}
      -DSOCI_DB2=OFF
      -DSOCI_FIREBIRD=OFF
      -DSOCI_MYSQL=OFF
      -DSOCI_ODBC=OFF
      -DSOCI_ORACLE=OFF
      -DSOCI_POSTGRESQL=OFF
      -DSOCI_SQLITE3=ON
      -DSQLITE3_INCLUDE_DIR=$<JOIN:$<TARGET_PROPERTY:sqlite,INTERFACE_INCLUDE_DIRECTORIES>,::>
      -DSQLITE3_LIBRARY=$<IF:$<CONFIG:Debug>,$<TARGET_PROPERTY:sqlite,IMPORTED_LOCATION_DEBUG>,$<TARGET_PROPERTY:sqlite,IMPORTED_LOCATION_RELEASE>>
      $<$<BOOL:${APPLE}>:-DCMAKE_FIND_FRAMEWORK=LAST>
      $<$<BOOL:${MSVC}>:
        "-DCMAKE_CXX_FLAGS=-GR -Gd -fp:precise -FS -EHa -MP"
        "-DCMAKE_CXX_FLAGS_DEBUG=-MTd"
        "-DCMAKE_CXX_FLAGS_RELEASE=-MT"
      >
      $<$<NOT:$<BOOL:${MSVC}>>:
        "-DCMAKE_CXX_FLAGS=-Wno-deprecated-declarations"
      >
      # SEE: https://github.com/SOCI/soci/issues/640
      $<$<AND:$<BOOL:${is_gcc}>,$<VERSION_GREATER_EQUAL:${CMAKE_CXX_COMPILER_VERSION},8>>:
        "-DCMAKE_CXX_FLAGS=-Wno-deprecated-declarations -Wno-error=format-overflow -Wno-format-overflow -Wno-error=format-truncation"
      >
    LIST_SEPARATOR ::
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
          <BINARY_DIR>/lib/$<CONFIG>/${soci_lib_pre}soci_core${soci_lib_post}$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
          <BINARY_DIR>/lib/$<CONFIG>/${soci_lib_pre}soci_empty${soci_lib_post}$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
          <BINARY_DIR>/lib/$<CONFIG>/${soci_lib_pre}soci_sqlite3${soci_lib_post}$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
          <BINARY_DIR>/lib
        >
    TEST_COMMAND ""
    INSTALL_COMMAND ""
    DEPENDS sqlite
    BUILD_BYPRODUCTS
      <BINARY_DIR>/lib/${soci_lib_pre}soci_core${soci_lib_post}${ep_lib_suffix}
      <BINARY_DIR>/lib/${soci_lib_pre}soci_core${soci_lib_post}_d${ep_lib_suffix}
      <BINARY_DIR>/lib/${soci_lib_pre}soci_empty${soci_lib_post}${ep_lib_suffix}
      <BINARY_DIR>/lib/${soci_lib_pre}soci_empty${soci_lib_post}_d${ep_lib_suffix}
      <BINARY_DIR>/lib/${soci_lib_pre}soci_sqlite3${soci_lib_post}${ep_lib_suffix}
      <BINARY_DIR>/lib/${soci_lib_pre}soci_sqlite3${soci_lib_post}_d${ep_lib_suffix}
  )
  ExternalProject_Get_Property (soci BINARY_DIR)
  ExternalProject_Get_Property (soci SOURCE_DIR)
  if (CMAKE_VERBOSE_MAKEFILE)
    print_ep_logs (soci)
  endif ()
  file (MAKE_DIRECTORY ${SOURCE_DIR}/include)
  file (MAKE_DIRECTORY ${BINARY_DIR}/include)
  foreach (_comp core empty sqlite3)
    set_target_properties ("soci_${_comp}" PROPERTIES
      IMPORTED_LOCATION_DEBUG
        ${BINARY_DIR}/lib/${soci_lib_pre}soci_${_comp}${soci_lib_post}_d${ep_lib_suffix}
      IMPORTED_LOCATION_RELEASE
        ${BINARY_DIR}/lib/${soci_lib_pre}soci_${_comp}${soci_lib_post}${ep_lib_suffix}
      INTERFACE_INCLUDE_DIRECTORIES
        "${SOURCE_DIR}/include;${BINARY_DIR}/include")
    add_dependencies ("soci_${_comp}" soci) # something has to depend on the ExternalProject to trigger it
    target_link_libraries (ripple_libs INTERFACE "soci_${_comp}")
    if (NOT _comp STREQUAL "core")
      target_link_libraries ("soci_${_comp}" INTERFACE soci_core)
    endif ()
  endforeach ()
endif()

foreach (_comp core empty sqlite3)
  exclude_if_included ("soci_${_comp}")
endforeach ()


exclude_if_included (soci)
