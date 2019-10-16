#[===================================================================[
   import protobuf (lib and compiler) and create a lib
   from our proto message definitions. If the system protobuf
   is not found, fallback on EP to download and build a version
   from official source.
#]===================================================================]

if (static)
  set (Protobuf_USE_STATIC_LIBS ON)
endif ()
find_package (Protobuf 3.8)
if (local_protobuf OR NOT Protobuf_FOUND)
  message (STATUS "using local protobuf build.")
  if (WIN32)
    # protobuf prepends lib even on windows
    set (pbuf_lib_pre "lib")
  else ()
    set (pbuf_lib_pre ${ep_lib_prefix})
  endif ()
  # for the external project build of protobuf, we currently ignore the
  # static option and always build static libs here. This is consistent
  # with our other EP builds. Dynamic libs in an EP would add complexity
  # because we'd need to get them into the runtime path, and probably
  # install them.
  ExternalProject_Add (protobuf_src
    PREFIX ${nih_cache_path}
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG v3.8.0
    SOURCE_SUBDIR cmake
    CMAKE_ARGS
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      -DCMAKE_INSTALL_PREFIX=<BINARY_DIR>/_installed_
      -Dprotobuf_BUILD_TESTS=OFF
      -Dprotobuf_BUILD_EXAMPLES=OFF
      -Dprotobuf_BUILD_PROTOC_BINARIES=ON
      -Dprotobuf_MSVC_STATIC_RUNTIME=ON
      -DBUILD_SHARED_LIBS=OFF
      -Dprotobuf_BUILD_SHARED_LIBS=OFF
      -DCMAKE_DEBUG_POSTFIX=_d
      -Dprotobuf_DEBUG_POSTFIX=_d
      -Dprotobuf_WITH_ZLIB=$<IF:$<BOOL:${has_zlib}>,ON,OFF>
      $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
      $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
      $<$<BOOL:${MSVC}>:
	"-DCMAKE_CXX_FLAGS=-GR -Gd -fp:precise -FS -EHa -MP"
      >
    LOG_BUILD ON
    LOG_CONFIGURE ON
    BUILD_COMMAND
      ${CMAKE_COMMAND}
      --build .
      --config $<CONFIG>
      $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel ${ep_procs}>
    TEST_COMMAND ""
    INSTALL_COMMAND
      ${CMAKE_COMMAND} -E env --unset=DESTDIR ${CMAKE_COMMAND} --build . --config $<CONFIG> --target install
    BUILD_BYPRODUCTS
      <BINARY_DIR>/_installed_/lib/${pbuf_lib_pre}protobuf${ep_lib_suffix}
      <BINARY_DIR>/_installed_/lib/${pbuf_lib_pre}protobuf_d${ep_lib_suffix}
      <BINARY_DIR>/_installed_/lib/${pbuf_lib_pre}protoc${ep_lib_suffix}
      <BINARY_DIR>/_installed_/lib/${pbuf_lib_pre}protoc_d${ep_lib_suffix}
      <BINARY_DIR>/_installed_/bin/protoc${CMAKE_EXECUTABLE_SUFFIX}
  )
  ExternalProject_Get_Property (protobuf_src BINARY_DIR)
  ExternalProject_Get_Property (protobuf_src SOURCE_DIR)
  if (CMAKE_VERBOSE_MAKEFILE)
    print_ep_logs (protobuf_src)
  endif ()
  exclude_if_included (protobuf_src)

  if (NOT TARGET protobuf::libprotobuf)
    add_library (protobuf::libprotobuf STATIC IMPORTED GLOBAL)
  endif ()
  file (MAKE_DIRECTORY ${BINARY_DIR}/_installed_/include)
  set_target_properties (protobuf::libprotobuf PROPERTIES
    IMPORTED_LOCATION_DEBUG
      ${BINARY_DIR}/_installed_/lib/${pbuf_lib_pre}protobuf_d${ep_lib_suffix}
    IMPORTED_LOCATION_RELEASE
      ${BINARY_DIR}/_installed_/lib/${pbuf_lib_pre}protobuf${ep_lib_suffix}
    INTERFACE_INCLUDE_DIRECTORIES
      ${BINARY_DIR}/_installed_/include)
  add_dependencies (protobuf::libprotobuf protobuf_src)
  exclude_if_included (protobuf::libprotobuf)

  if (NOT TARGET protobuf::libprotoc)
    add_library (protobuf::libprotoc STATIC IMPORTED GLOBAL)
  endif ()
  set_target_properties (protobuf::libprotoc PROPERTIES
    IMPORTED_LOCATION_DEBUG
      ${BINARY_DIR}/_installed_/lib/${pbuf_lib_pre}protoc_d${ep_lib_suffix}
    IMPORTED_LOCATION_RELEASE
      ${BINARY_DIR}/_installed_/lib/${pbuf_lib_pre}protoc${ep_lib_suffix}
    INTERFACE_INCLUDE_DIRECTORIES
      ${BINARY_DIR}/_installed_/include)
  add_dependencies (protobuf::libprotoc protobuf_src)
  exclude_if_included (protobuf::libprotoc)

  if (NOT TARGET protobuf::protoc)
    add_executable (protobuf::protoc IMPORTED)
    exclude_if_included (protobuf::protoc)
  endif ()
  set_target_properties (protobuf::protoc PROPERTIES
    IMPORTED_LOCATION "${BINARY_DIR}/_installed_/bin/protoc${CMAKE_EXECUTABLE_SUFFIX}")
  add_dependencies (protobuf::protoc protobuf_src)
else ()
  if (NOT TARGET protobuf::protoc)
    if (EXISTS "${Protobuf_PROTOC_EXECUTABLE}")
      add_executable (protobuf::protoc IMPORTED)
      set_target_properties (protobuf::protoc PROPERTIES
        IMPORTED_LOCATION "${Protobuf_PROTOC_EXECUTABLE}")
    else ()
      message (FATAL_ERROR "Protobuf import failed")
    endif ()
  endif ()
endif ()

file (MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/proto_gen)
set (save_CBD ${CMAKE_CURRENT_BINARY_DIR})
set (CMAKE_CURRENT_BINARY_DIR ${CMAKE_BINARY_DIR}/proto_gen)
protobuf_generate_cpp (
  PROTO_SRCS
  PROTO_HDRS
  src/ripple/proto/ripple.proto)
set (CMAKE_CURRENT_BINARY_DIR ${save_CBD})

add_library (pbufs STATIC ${PROTO_SRCS} ${PROTO_HDRS})

target_include_directories (pbufs PRIVATE src)
target_include_directories (pbufs
  SYSTEM PUBLIC ${CMAKE_BINARY_DIR}/proto_gen)
target_link_libraries (pbufs protobuf::libprotobuf)
target_compile_options (pbufs
  PUBLIC
    $<$<BOOL:${is_xcode}>:
      --system-header-prefix="google/protobuf"
      -Wno-deprecated-dynamic-exception-spec
    >)
add_library (Ripple::pbufs ALIAS pbufs)
target_link_libraries (ripple_libs INTERFACE Ripple::pbufs)
exclude_if_included (pbufs)
