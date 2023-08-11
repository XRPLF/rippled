find_package(Protobuf 3.8)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/proto_gen)
set(ccbd ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_CURRENT_BINARY_DIR ${CMAKE_BINARY_DIR}/proto_gen)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS src/ripple/proto/ripple.proto)
set(CMAKE_CURRENT_BINARY_DIR ${ccbd})

add_library(pbufs STATIC ${PROTO_SRCS} ${PROTO_HDRS})
target_include_directories(pbufs SYSTEM PUBLIC
  ${CMAKE_BINARY_DIR}/proto_gen
  ${CMAKE_BINARY_DIR}/proto_gen/src/ripple/proto
)
target_link_libraries(pbufs protobuf::libprotobuf)
target_compile_options(pbufs
  PUBLIC
    $<$<BOOL:${XCODE}>:
      --system-header-prefix="google/protobuf"
      -Wno-deprecated-dynamic-exception-spec
    >
)
add_library(Ripple::pbufs ALIAS pbufs)
