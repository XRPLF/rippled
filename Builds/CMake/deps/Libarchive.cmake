#[===================================================================[
   NIH dep: libarchive
#]===================================================================]

option (local_libarchive "use local build of libarchive." OFF)
add_library (archive_lib UNKNOWN IMPORTED GLOBAL)

if (NOT local_libarchive)
  if (NOT WIN32)
    find_package(libarchive_pc REQUIRED)
  endif ()
  if (archive)
    message (STATUS "Found libarchive using pkg-config. Using ${archive}.")
    set_target_properties (archive_lib PROPERTIES
      IMPORTED_LOCATION_DEBUG
        ${archive}
      IMPORTED_LOCATION_RELEASE
        ${archive}
      INTERFACE_INCLUDE_DIRECTORIES
        ${LIBARCHIVE_INCLUDE_DIR})
      # pkg-config can return extra info for static lib linking
      # this is probably needed/useful generally, but apply
      # to APPLE for now (mostly for homebrew)
      if (APPLE AND static AND libarchive_PC_STATIC_LIBRARIES)
        message(STATUS "NOTE: libarchive static libs: ${libarchive_PC_STATIC_LIBRARIES}")
        # also, APPLE seems to need iconv...maybe linux does too (TBD)
        target_link_libraries (archive_lib
          INTERFACE iconv ${libarchive_PC_STATIC_LIBRARIES})
      endif ()
  else ()
    ## now try searching using the minimal find module that cmake provides
    find_package(LibArchive 3.4.3 QUIET)
    if (LibArchive_FOUND)
      if (static)
        # find module doesn't find static libs currently, so we re-search
        get_filename_component(_loc ${LibArchive_LIBRARY} DIRECTORY)
        find_library(_la_static
          NAMES libarchive.a archive_static.lib archive.lib
          PATHS ${_loc})
        if (_la_static)
          set (_la_lib ${_la_static})
        else ()
          message (WARNING "unable to find libarchive static lib - switching to local build")
          set (local_libarchive ON CACHE BOOL "" FORCE)
        endif ()
      else ()
        set (_la_lib ${LibArchive_LIBRARY})
      endif ()
      if (NOT local_libarchive)
        message (STATUS "Found libarchive using module/config. Using ${_la_lib}.")
        set_target_properties (archive_lib PROPERTIES
          IMPORTED_LOCATION_DEBUG
            ${_la_lib}
          IMPORTED_LOCATION_RELEASE
            ${_la_lib}
          INTERFACE_INCLUDE_DIRECTORIES
            ${LibArchive_INCLUDE_DIRS})
      endif ()
    else ()
      set (local_libarchive ON CACHE BOOL "" FORCE)
    endif ()
  endif ()
endif()

if (local_libarchive)
  set (lib_post "")
  if (MSVC)
    set (lib_post "_static")
  endif ()
  ExternalProject_Add (libarchive
    PREFIX ${nih_cache_path}
    GIT_REPOSITORY https://github.com/libarchive/libarchive.git
    GIT_TAG v3.4.3
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
      --parallel ${ep_procs}
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
