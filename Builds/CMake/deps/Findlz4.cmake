find_package (PkgConfig REQUIRED)
pkg_search_module (lz4_PC QUIET liblz4>=1.8)

if(static)
  set(LZ4_LIB liblz4.a)
else()
  set(LZ4_LIB lz4)
endif()

find_library (lz4
  NAMES ${LZ4_LIB}
  HINTS
    ${lz4_PC_LIBDIR}
    ${lz4_PC_LIBRARY_DIRS}
  NO_DEFAULT_PATH)

find_path (LZ4_INCLUDE_DIR
  NAMES lz4.h
  HINTS
    ${lz4_PC_INCLUDEDIR}
    ${lz4_PC_INCLUDEDIRS}
  NO_DEFAULT_PATH)
