#[===================================================================[
   NIH dep: libarchive
#]===================================================================]

add_library (archive_lib STATIC IMPORTED GLOBAL)

if (NOT WIN32)
  find_package(libarchive REQUIRED)
endif()

if(libarchive)
  set_target_properties (archive_lib PROPERTIES
    IMPORTED_LOCATION_DEBUG
      ${archive}
    IMPORTED_LOCATION_RELEASE
      ${archive}
    INTERFACE_INCLUDE_DIRECTORIES
      ${LIBARCHIVE_INCLUDE_DIR})

else()
  set (lib_post "")
  if (MSVC)
    set (lib_post "_static")
  endif ()
  ExternalProject_Add (libarchive
    PREFIX ${nih_cache_path}
    GIT_REPOSITORY https://github.com/libarchive/libarchive.git
    GIT_TAG v3.3.3
    CMAKE_ARGS
      # passing the compiler seems to be needed for windows CI, sadly
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
      -DCMAKE_DEBUG_POSTFIX=_d
      $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
      -DENABLE_LZ4=ON
      -ULZ4_*
      -DLZ4_INCLUDE_DIR=$<JOIN:$<TARGET_PROPERTY:lz4_lib,INTERFACE_INCLUDE_DIRECTORIES>,::>
      # because we are building a static lib, this lz4 library doesn't
      # actually matter since you can't generally link static libs to other static
      # libs. The include files are needed, but the library itself is not (until
      # we link our application, at which point we use the lz4 we built above).
      # nonetheless, we need to provide a library to libarchive else it will
      # NOT include lz4 support when configuring
      -DLZ4_LIBRARY=$<IF:$<CONFIG:Debug>,$<TARGET_PROPERTY:lz4_lib,IMPORTED_LOCATION_DEBUG>,$<TARGET_PROPERTY:lz4_lib,IMPORTED_LOCATION_RELEASE>>
      -DENABLE_WERROR=OFF
      -DENABLE_TAR=OFF
      -DENABLE_TAR_SHARED=OFF
      -DENABLE_INSTALL=ON
      -DENABLE_NETTLE=OFF
      -DENABLE_OPENSSL=OFF
      -DENABLE_LZO=OFF
      -DENABLE_LZMA=OFF
      -DENABLE_ZLIB=OFF
      -DENABLE_BZip2=OFF
      -DENABLE_LIBXML2=OFF
      -DENABLE_EXPAT=OFF
      -DENABLE_PCREPOSIX=OFF
      -DENABLE_LibGCC=OFF
      -DENABLE_CNG=OFF
      -DENABLE_CPIO=OFF
      -DENABLE_CPIO_SHARED=OFF
      -DENABLE_CAT=OFF
      -DENABLE_CAT_SHARED=OFF
      -DENABLE_XATTR=OFF
      -DENABLE_ACL=OFF
      -DENABLE_ICONV=OFF
      -DENABLE_TEST=OFF
      -DENABLE_COVERAGE=OFF
      $<$<BOOL:${MSVC}>:
        "-DCMAKE_C_FLAGS=-GR -Gd -fp:precise -FS -MP"
        "-DCMAKE_C_FLAGS_DEBUG=-MTd"
        "-DCMAKE_C_FLAGS_RELEASE=-MT"
      >
    LIST_SEPARATOR ::
    LOG_BUILD ON
    LOG_CONFIGURE ON
    BUILD_COMMAND
      ${CMAKE_COMMAND}
      --build .
      --config $<CONFIG>
      --target archive_static
      $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel ${ep_procs}>
      $<$<BOOL:${is_multiconfig}>:
        COMMAND
          ${CMAKE_COMMAND} -E copy
            <BINARY_DIR>/libarchive/$<CONFIG>/${ep_lib_prefix}archive${lib_post}$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
            <BINARY_DIR>/libarchive
        >
    TEST_COMMAND ""
    INSTALL_COMMAND ""
    DEPENDS lz4_lib
    BUILD_BYPRODUCTS
      <BINARY_DIR>/libarchive/${ep_lib_prefix}archive${lib_post}${ep_lib_suffix}
      <BINARY_DIR>/libarchive/${ep_lib_prefix}archive${lib_post}_d${ep_lib_suffix}
  )
  ExternalProject_Get_Property (libarchive BINARY_DIR)
  ExternalProject_Get_Property (libarchive SOURCE_DIR)
  if (CMAKE_VERBOSE_MAKEFILE)
    print_ep_logs (libarchive)
  endif ()
  file (MAKE_DIRECTORY ${SOURCE_DIR}/libarchive)
  set_target_properties (archive_lib PROPERTIES
    IMPORTED_LOCATION_DEBUG
      ${BINARY_DIR}/libarchive/${ep_lib_prefix}archive${lib_post}_d${ep_lib_suffix}
    IMPORTED_LOCATION_RELEASE
      ${BINARY_DIR}/libarchive/${ep_lib_prefix}archive${lib_post}${ep_lib_suffix}
    INTERFACE_INCLUDE_DIRECTORIES
      ${SOURCE_DIR}/libarchive
    INTERFACE_COMPILE_DEFINITIONS
      LIBARCHIVE_STATIC)
endif()

add_dependencies (archive_lib libarchive)
target_link_libraries (archive_lib INTERFACE lz4_lib)
target_link_libraries (ripple_libs INTERFACE archive_lib)
exclude_if_included (libarchive)
exclude_if_included (archive_lib)
