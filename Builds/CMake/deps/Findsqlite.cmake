find_package (PkgConfig)
if (PKG_CONFIG_FOUND)
  pkg_search_module (sqlite_PC QUIET sqlite3>=3.26.0)
endif ()

if(static)
  set(SQLITE_LIB libsqlite3.a)
else()
  set(SQLITE_LIB sqlite3.so)
endif()

find_library (sqlite3
  NAMES ${SQLITE_LIB}
  HINTS
    ${sqlite_PC_LIBDIR}
    ${sqlite_PC_LIBRARY_DIRS}
  NO_DEFAULT_PATH)

find_path (SQLITE_INCLUDE_DIR
  NAMES sqlite3.h
  HINTS
    ${sqlite_PC_INCLUDEDIR}
    ${sqlite_PC_INCLUDEDIRS}
  NO_DEFAULT_PATH)
