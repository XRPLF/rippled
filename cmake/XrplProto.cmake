#[===================================================================[
   Protobuf - Regeneration of the committed protobuf/gRPC sources.

   The generated `.pb.{h,cc}` and `.grpc.pb.{h,cc}` files are committed to the
   repository (headers under `include/xrpl/proto_generated`, sources under
   `src/libxrpl/proto_generated`) and compiled directly by the `xrpl.libpb`
   target (see cmake/XrplCore.cmake). They are NOT generated at build time, so a
   normal build (and tools such as clang-tidy) do not need protoc.

   Run the generation manually after changing any `.proto` file with:
     cmake --build . --target proto_gen
   and commit the regenerated files. The up-to-date check in CI verifies that
   the committed files match the `.proto` sources.
#]===================================================================]

# `.proto` sources live here; generated headers and sources are written to
# separate directories (see cmake/XrplProtoRun.cmake).
set(PROTO_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/xrpl/proto")
set(PROTO_HEADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/xrpl/proto_generated")
set(PROTO_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/libxrpl/proto_generated")

file(GLOB PROTO_ROOT_FILES "${PROTO_SRC_DIR}/*.proto")
file(GLOB_RECURSE PROTO_GRPC_FILES "${PROTO_SRC_DIR}/org/*.proto")

# Custom target for protobuf code generation, excluded from ALL.
# Run manually with: cmake --build . --target proto_gen
add_custom_target(
    proto_gen
    COMMAND
        ${CMAKE_COMMAND} -DPROTOC=$<TARGET_FILE:protobuf::protoc>
        -DGRPC_PLUGIN=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        -DPROTO_SRC_DIR=${PROTO_SRC_DIR} -DPROTO_HEADER_DIR=${PROTO_HEADER_DIR}
        -DPROTO_SOURCE_DIR=${PROTO_SOURCE_DIR}
        -DSTAGING_DIR=${CMAKE_BINARY_DIR}/proto_gen_staging -P
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/XrplProtoRun.cmake"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Regenerating committed protobuf sources..."
    VERBATIM
    SOURCES ${PROTO_ROOT_FILES} ${PROTO_GRPC_FILES}
)

# Single entry point to regenerate every committed generated source: the
# protocol wrapper classes (code_gen) and the protobuf/gRPC sources (proto_gen).
# Run with: cmake --build . --target generate
add_custom_target(
    generate
    COMMENT "Regenerating all committed generated sources..."
)
add_dependencies(generate proto_gen)
# code_gen is only defined when Python3 is available (see XrplProtocolAutogen).
if(TARGET code_gen)
    add_dependencies(generate code_gen)
endif()
