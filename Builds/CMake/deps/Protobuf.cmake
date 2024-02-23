find_package(Protobuf 3.8)

set(output_dir ${CMAKE_BINARY_DIR}/proto_gen)
file(MAKE_DIRECTORY ${output_dir})
set(ccbd ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_CURRENT_BINARY_DIR ${output_dir})
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS src/ripple/proto/ripple.proto)
set(CMAKE_CURRENT_BINARY_DIR ${ccbd})

target_include_directories(xrpl_core SYSTEM PUBLIC
    # The generated implementation imports the header relative to the output
    # directory.
    $<BUILD_INTERFACE:${output_dir}>
    $<BUILD_INTERFACE:${output_dir}/src>
)
target_sources(xrpl_core PRIVATE ${output_dir}/src/ripple/proto/ripple.pb.cc)
install(
  FILES ${output_dir}/src/ripple/proto/ripple.pb.h
  DESTINATION include/ripple/proto)
target_link_libraries(xrpl_core PUBLIC protobuf::libprotobuf)
target_compile_options(xrpl_core
  PUBLIC
    $<$<BOOL:${XCODE}>:
      --system-header-prefix="google/protobuf"
      -Wno-deprecated-dynamic-exception-spec
    >
)
