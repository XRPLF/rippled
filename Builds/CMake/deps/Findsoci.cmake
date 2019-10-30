find_package (PkgConfig REQUIRED)

# no soci pkgconfig
#pkg_search_module (soci_PC QUIET libsoci_core>=3.2)

if(static)
  set(SOCI_LIB libsoci.a)
else()
  set(SOCI_LIB libsoci_core.so)
endif()

find_library (soci
  NAMES ${SOCI_LIB})

find_path (SOCI_INCLUDE_DIR
  NAMES soci/soci.h)
