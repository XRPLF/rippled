#[===================================================================[
   NIH dep: rocksdb
#]===================================================================]

ExternalProject_Add (rocksdb
  PREFIX ${nih_cache_path}
  GIT_REPOSITORY https://github.com/facebook/rocksdb.git
  GIT_TAG v5.17.2
  PATCH_COMMAND
    # only used by windows build
    ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/Builds/CMake/rocks_thirdparty.inc
    <SOURCE_DIR>/thirdparty.inc
  COMMAND
    # fixup their build version file to keep the values
    # from changing always
    ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/Builds/CMake/rocksdb_build_version.cc.in
    <SOURCE_DIR>/util/build_version.cc.in
  CMAKE_ARGS
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
    -DCMAKE_DEBUG_POSTFIX=_d
    $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
    -DBUILD_SHARED_LIBS=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DWITH_JEMALLOC=$<IF:$<BOOL:${jemalloc}>,ON,OFF>
    -DWITH_SNAPPY=ON
    -DWITH_LZ4=ON
    -DWITH_ZLIB=OFF
    -DUSE_RTTI=ON
    -DWITH_ZSTD=OFF
    -DWITH_GFLAGS=OFF
    -DWITH_BZ2=OFF
    -ULZ4_*
    -DLZ4_INCLUDE_DIR=$<JOIN:$<TARGET_PROPERTY:lz4_lib,INTERFACE_INCLUDE_DIRECTORIES>,::>
    -DLZ4_LIBRARIES=$<IF:$<CONFIG:Debug>,$<TARGET_PROPERTY:lz4_lib,IMPORTED_LOCATION_DEBUG>,$<TARGET_PROPERTY:lz4_lib,IMPORTED_LOCATION_RELEASE>>
    -DLZ4_FOUND=ON
    -USNAPPY_*
    -DSNAPPY_INCLUDE_DIR=$<JOIN:$<TARGET_PROPERTY:snappy_lib,INTERFACE_INCLUDE_DIRECTORIES>,::>
    -DSNAPPY_LIBRARIES=$<IF:$<CONFIG:Debug>,$<TARGET_PROPERTY:snappy_lib,IMPORTED_LOCATION_DEBUG>,$<TARGET_PROPERTY:snappy_lib,IMPORTED_LOCATION_RELEASE>>
    -DSNAPPY_FOUND=ON
    -DWITH_MD_LIBRARY=OFF
    -DWITH_RUNTIME_DEBUG=$<IF:$<CONFIG:Debug>,ON,OFF>
    -DFAIL_ON_WARNINGS=OFF
    -DWITH_ASAN=OFF
    -DWITH_TSAN=OFF
    -DWITH_UBSAN=OFF
    -DWITH_NUMA=OFF
    -DWITH_TBB=OFF
    -DWITH_WINDOWS_UTF8_FILENAMES=OFF
    -DWITH_XPRESS=OFF
    -DPORTABLE=ON
    -DFORCE_SSE42=OFF
    -DDISABLE_STALL_NOTIF=OFF
    -DOPTDBG=ON
    -DROCKSDB_LITE=OFF
    -DWITH_FALLOCATE=ON
    -DWITH_LIBRADOS=OFF
    -DWITH_JNI=OFF
    -DROCKSDB_INSTALL_ON_WINDOWS=OFF
    -DWITH_TESTS=OFF
    -DWITH_TOOLS=OFF
    $<$<BOOL:${MSVC}>:
      "-DCMAKE_CXX_FLAGS=-GR -Gd -fp:precise -FS -MP /DNDEBUG"
    >
    $<$<NOT:$<BOOL:${MSVC}>>:
      "-DCMAKE_CXX_FLAGS=-DNDEBUG"
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
        <BINARY_DIR>/$<CONFIG>/${ep_lib_prefix}rocksdb$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
        <BINARY_DIR>
      >
  LIST_SEPARATOR ::
  TEST_COMMAND ""
  INSTALL_COMMAND ""
  DEPENDS snappy lz4
  BUILD_BYPRODUCTS
    <BINARY_DIR>/${ep_lib_prefix}rocksdb${ep_lib_suffix}
    <BINARY_DIR>/${ep_lib_prefix}rocksdb_d${ep_lib_suffix}
)
ExternalProject_Get_Property (rocksdb BINARY_DIR)
ExternalProject_Get_Property (rocksdb SOURCE_DIR)
if (CMAKE_VERBOSE_MAKEFILE)
  print_ep_logs (rocksdb)
endif ()
add_library (rocksdb_lib STATIC IMPORTED GLOBAL)
file (MAKE_DIRECTORY ${SOURCE_DIR}/include)
set_target_properties (rocksdb_lib PROPERTIES
  IMPORTED_LOCATION_DEBUG
    ${BINARY_DIR}/${ep_lib_prefix}rocksdb_d${ep_lib_suffix}
  IMPORTED_LOCATION_RELEASE
    ${BINARY_DIR}/${ep_lib_prefix}rocksdb${ep_lib_suffix}
  INTERFACE_INCLUDE_DIRECTORIES
    ${SOURCE_DIR}/include
  INTERFACE_COMPILE_DEFINITIONS
    RIPPLE_ROCKSDB_AVAILABLE=1)
add_dependencies (rocksdb_lib rocksdb)
target_link_libraries (rocksdb_lib INTERFACE snappy_lib lz4_lib)
if (MSVC)
  target_link_libraries (rocksdb_lib INTERFACE rpcrt4)
endif ()
target_link_libraries (ripple_libs INTERFACE rocksdb_lib)
exclude_if_included (rocksdb)
exclude_if_included (rocksdb_lib)
