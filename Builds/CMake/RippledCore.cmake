#[===================================================================[
  Exported targets.
#]===================================================================]

include(target_protobuf_sources)

# Protocol buffers cannot participate in a unity build,
# because all the generated sources
# define a bunch of `static const` variables with the same names,
# so we just build them as a separate library.
add_library(xrpl.libpb)
target_protobuf_sources(xrpl.libpb xrpl/proto
  LANGUAGE cpp
  IMPORT_DIRS include/xrpl/proto
  PROTOS include/xrpl/proto/ripple.proto
)

file(GLOB_RECURSE protos "include/xrpl/proto/org/*.proto")
target_protobuf_sources(xrpl.libpb xrpl/proto
  LANGUAGE cpp
  IMPORT_DIRS include/xrpl/proto
  PROTOS "${protos}"
)
target_protobuf_sources(xrpl.libpb xrpl/proto
  LANGUAGE grpc
  IMPORT_DIRS include/xrpl/proto
  PROTOS "${protos}"
  PLUGIN protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
  GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
)

target_compile_options(xrpl.libpb
  PUBLIC
    $<$<BOOL:${MSVC}>:-wd4996>
    $<$<BOOL:${XCODE}>:
      --system-header-prefix="google/protobuf"
      -Wno-deprecated-dynamic-exception-spec
    >
  PRIVATE
    $<$<BOOL:${MSVC}>:-wd4065>
    $<$<NOT:$<BOOL:${MSVC}>>:-Wno-deprecated-declarations>
)

target_link_libraries(xrpl.libpb
  PUBLIC
    protobuf::libprotobuf
    gRPC::grpc++
)

add_library(xrpl.libxrpl)
set_target_properties(xrpl.libxrpl PROPERTIES OUTPUT_NAME xrpl)
if(unity)
  set_target_properties(xrpl.libxrpl PROPERTIES UNITY_BUILD ON)
endif()

add_library(xrpl::libxrpl ALIAS xrpl.libxrpl)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/libxrpl/*.cpp"
)
target_sources(xrpl.libxrpl PRIVATE ${sources})

target_include_directories(xrpl.libxrpl
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_compile_definitions(xrpl.libxrpl
  PUBLIC
    BOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT
    BOOST_CONTAINER_FWD_BAD_DEQUE
    HAS_UNCAUGHT_EXCEPTIONS=1)

target_compile_options(xrpl.libxrpl
  PUBLIC
    $<$<BOOL:${is_gcc}>:-Wno-maybe-uninitialized>
)

target_link_libraries(xrpl.libxrpl
  PUBLIC
    LibArchive::LibArchive
    OpenSSL::Crypto
    Ripple::boost
    Ripple::opts
    Ripple::syslibs
    absl::random_random
    date::date
    ed25519::ed25519
    secp256k1::secp256k1
    xrpl.libpb
    xxHash::xxhash
)

add_executable(rippled)
if(unity)
  set_target_properties(rippled PROPERTIES UNITY_BUILD ON)
endif()
if(tests)
  target_compile_definitions(rippled PUBLIC ENABLE_TESTS)
endif()
target_include_directories(rippled
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/xrpld/*.cpp"
)
target_sources(rippled PRIVATE ${sources})

if(tests)
  file(GLOB_RECURSE sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test/*.cpp"
  )
  target_sources(rippled PRIVATE ${sources})
endif()

target_link_libraries(rippled
  Ripple::boost
  Ripple::opts
  Ripple::libs
  xrpl.libxrpl
)
exclude_if_included(rippled)
# define a macro for tests that might need to
# be exluded or run differently in CI environment
if(is_ci)
  target_compile_definitions(rippled PRIVATE RIPPLED_RUNNING_IN_CI)
endif ()

if(reporting)
  set(suffix -reporting)
  set_target_properties(rippled PROPERTIES OUTPUT_NAME rippled-reporting)
  get_target_property(BIN_NAME rippled OUTPUT_NAME)
  message(STATUS "Reporting mode build: rippled renamed ${BIN_NAME}")
  target_compile_definitions(rippled PRIVATE RIPPLED_REPORTING)
endif()

# any files that don't play well with unity should be added here
if(tests)
  set_source_files_properties(
    # these two seem to produce conflicts in beast teardown template methods
    src/test/rpc/ValidatorRPC_test.cpp
    src/test/rpc/ShardArchiveHandler_test.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE)
endif()
