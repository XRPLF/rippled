#[===================================================================[
   Protobuf - Run script invoked by the 'proto_gen' target.

   protoc emits the header (`.pb.h`) and implementation (`.pb.cc`) for each
   `.proto` into the same output directory, so we generate into a staging
   directory and then distribute the results to match the repository layout:
   headers go under `include/xrpl/proto_generated`, sources under
   `src/libxrpl/proto_generated`.

   Expects the following -D variables: PROTOC, GRPC_PLUGIN, PROTO_SRC_DIR,
   PROTO_HEADER_DIR, PROTO_SOURCE_DIR, STAGING_DIR.
#]===================================================================]

# `xrpl.proto` generates only C++ message classes. The `org/*` protos generate
# C++ message classes as well as gRPC service stubs.
file(GLOB PROTO_ROOT_FILES "${PROTO_SRC_DIR}/*.proto")
file(GLOB_RECURSE PROTO_GRPC_FILES "${PROTO_SRC_DIR}/org/*.proto")

file(REMOVE_RECURSE "${STAGING_DIR}")
file(MAKE_DIRECTORY "${STAGING_DIR}")

execute_process(
    COMMAND
        "${PROTOC}" --proto_path "${PROTO_SRC_DIR}" --cpp_out "${STAGING_DIR}"
        ${PROTO_ROOT_FILES} ${PROTO_GRPC_FILES}
    RESULT_VARIABLE CPP_RESULT
)
if(NOT CPP_RESULT EQUAL 0)
    message(FATAL_ERROR "C++ protobuf generation failed: ${CPP_RESULT}")
endif()

execute_process(
    COMMAND
        "${PROTOC}" --proto_path "${PROTO_SRC_DIR}" --grpc_out "${STAGING_DIR}"
        "--plugin=protoc-gen-grpc=${GRPC_PLUGIN}" ${PROTO_GRPC_FILES}
    RESULT_VARIABLE GRPC_RESULT
)
if(NOT GRPC_RESULT EQUAL 0)
    message(FATAL_ERROR "gRPC protobuf generation failed: ${GRPC_RESULT}")
endif()

# Distribute the generated files, preserving their relative paths.
file(GLOB_RECURSE GEN_HEADERS RELATIVE "${STAGING_DIR}" "${STAGING_DIR}/*.h")
file(GLOB_RECURSE GEN_SOURCES RELATIVE "${STAGING_DIR}" "${STAGING_DIR}/*.cc")
foreach(header ${GEN_HEADERS})
    get_filename_component(dir "${PROTO_HEADER_DIR}/${header}" DIRECTORY)
    file(MAKE_DIRECTORY "${dir}")
    file(RENAME "${STAGING_DIR}/${header}" "${PROTO_HEADER_DIR}/${header}")
endforeach()
foreach(source ${GEN_SOURCES})
    get_filename_component(dir "${PROTO_SOURCE_DIR}/${source}" DIRECTORY)
    file(MAKE_DIRECTORY "${dir}")
    file(RENAME "${STAGING_DIR}/${source}" "${PROTO_SOURCE_DIR}/${source}")
endforeach()

file(REMOVE_RECURSE "${STAGING_DIR}")

message(STATUS "Protobuf generation complete")
