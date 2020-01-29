set (RocksDB_DIR "" CACHE PATH "Root directory of RocksDB distribution")

find_path (RocksDB_INCLUDE_DIR
    rocksdb/db.h
    PATHS ${RocksDB_DIR})

set (RocksDB_VERSION "")
find_file (RocksDB_VERSION_FILE
    rocksdb/version.h
    PATHS ${RocksDB_DIR})
if (RocksDB_VERSION_FILE)
    file (READ ${RocksDB_VERSION_FILE} _verfile)
    if ("${_verfile}" MATCHES "#define[ \\t]+ROCKSDB_MAJOR[ \\t]+([0-9]+)")
        string (APPEND RocksDB_VERSION "${CMAKE_MATCH_1}")
    else ()
        string (APPEND RocksDB_VERSION "0")
    endif()
    if ("${_verfile}" MATCHES "#define[ \\t]+ROCKSDB_MINOR[ \\t]+([0-9]+)")
        string (APPEND RocksDB_VERSION ".${CMAKE_MATCH_1}")
    else ()
        string (APPEND RocksDB_VERSION ".0")
    endif()
    if ("${_verfile}" MATCHES "#define[ \\t]+ROCKSDB_PATCH[ \\t]+([0-9]+)")
        string (APPEND RocksDB_VERSION ".${CMAKE_MATCH_1}")
    else ()
        string (APPEND RocksDB_VERSION ".0")
    endif()
endif ()

if (RocksDB_USE_STATIC)
    list (APPEND RocksDB_NAMES
        "${CMAKE_STATIC_LIBRARY_PREFIX}rocksdb${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "${CMAKE_STATIC_LIBRARY_PREFIX}rocksdblib${CMAKE_STATIC_LIBRARY_SUFFIX}")
endif ()

list (APPEND RocksDB_NAMES rocksdb)

find_library (RocksDB_LIBRARY NAMES ${RocksDB_NAMES}
    PATHS
    ${RocksDB_DIR}
    ${RocksDB_DIR}/bin/Release
    ${RocksDB_DIR}/bin64_vs2013/Release
    PATH_SUFFIXES lib lib64)

foreach (_n RocksDB_NAMES)
    list (APPEND RocksDB_NAMES_DBG "${_n}_d" "${_n}d")
endforeach ()
find_library (RocksDB_LIBRARY_DEBUG NAMES ${RocksDB_NAMES_DBG}
    PATHS
    ${RocksDB_DIR}
    ${RocksDB_DIR}/bin/Debug
    ${RocksDB_DIR}/bin64_vs2013/Debug
    PATH_SUFFIXES lib lib64)

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (RocksDB
    REQUIRED_VARS RocksDB_LIBRARY RocksDB_INCLUDE_DIR
    VERSION_VAR RocksDB_VERSION)

mark_as_advanced (RocksDB_INCLUDE_DIR RocksDB_LIBRARY)
set (RocksDB_INCLUDE_DIRS ${RocksDB_INCLUDE_DIR})
set (RocksDB_LIBRARIES ${RocksDB_LIBRARY})
