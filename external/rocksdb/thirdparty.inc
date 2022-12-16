if(WITH_GFLAGS)
  # Config with namespace available since gflags 2.2.2
  find_package(gflags REQUIRED)
  set(GFLAGS_LIB gflags::gflags)
  list(APPEND THIRDPARTY_LIBS ${GFLAGS_LIB})
  add_definitions(-DGFLAGS=1)
endif()

if(WITH_SNAPPY)
  find_package(Snappy REQUIRED)
  add_definitions(-DSNAPPY)
  list(APPEND THIRDPARTY_LIBS Snappy::snappy)
endif()

if(WITH_LZ4)
  find_package(lz4 REQUIRED)
  add_definitions(-DLZ4)
  list(APPEND THIRDPARTY_LIBS lz4::lz4)
endif()

if(WITH_ZLIB)
  find_package(ZLIB REQUIRED)
  add_definitions(-DZLIB)
  list(APPEND THIRDPARTY_LIBS ZLIB::ZLIB)
endif()

option(WITH_BZ2 "build with bzip2" OFF)
if(WITH_BZ2)
  find_package(BZip2 REQUIRED)
  add_definitions(-DBZIP2)
  list(APPEND THIRDPARTY_LIBS BZip2::BZip2)
endif()

if(WITH_ZSTD)
  find_package(zstd REQUIRED)
  add_definitions(-DZSTD)
  list(APPEND THIRDPARTY_LIBS zstd::zstd)
endif()

# ================================================== XPRESS ==================================================
# This makes use of built-in Windows API, no additional includes, links to a system lib

if(WITH_XPRESS)
  message(STATUS "XPRESS is enabled")
  add_definitions(-DXPRESS)
  # We are using the implementation provided by the system
  list(APPEND SYSTEM_LIBS Cabinet.lib)
else()
  message(STATUS "XPRESS is disabled")
endif()

# ================================================== JEMALLOC ==================================================
if(WITH_JEMALLOC)
  message(STATUS "JEMALLOC library is enabled")
  add_definitions(-DROCKSDB_JEMALLOC -DJEMALLOC_EXPORT= -DJEMALLOC_NO_RENAME)
  list(APPEND THIRDPARTY_LIBS jemalloc::jemalloc)
  set(ARTIFACT_SUFFIX "_je")

else ()
  set(ARTIFACT_SUFFIX "")
  message(STATUS "JEMALLOC library is disabled")
endif ()
