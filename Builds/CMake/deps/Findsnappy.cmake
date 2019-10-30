find_package (PkgConfig REQUIRED)
pkg_search_module (snappy_PC QUIET snappy>=1.1.7)

if(static)
  set(SNAPPY_LIB libsnappy.a)
else()
  set(SNAPPY_LIB libsnappy.so)
endif()

find_library (snappy
  NAMES ${SNAPPY_LIB}
  HINTS
    ${snappy_PC_LIBDIR}
    ${snappy_PC_LIBRARY_DIRS}
  NO_DEFAULT_PATH)

find_path (SNAPPY_INCLUDE_DIR
  NAMES snappy.h
  HINTS
    ${snappy_PC_INCLUDEDIR}
    ${snappy_PC_INCLUDEDIRS}
  NO_DEFAULT_PATH)
