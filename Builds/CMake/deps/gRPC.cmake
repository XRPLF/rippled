find_package(gRPC 1.23)

#[=================================[
   generate protobuf sources for
   grpc defs and bundle into a
   static lib
#]=================================]
set(output_dir "${CMAKE_BINARY_DIR}/proto_gen_grpc")
set(GRPC_GEN_DIR "${output_dir}/ripple/proto")
file(MAKE_DIRECTORY ${GRPC_GEN_DIR})
set(GRPC_PROTO_SRCS)
set(GRPC_PROTO_HDRS)
set(GRPC_PROTO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/ripple/proto/org")
file(GLOB_RECURSE GRPC_DEFINITION_FILES "${GRPC_PROTO_ROOT}/*.proto")
foreach(file ${GRPC_DEFINITION_FILES})
  # /home/user/rippled/src/ripple/proto/org/.../v1/get_ledger.proto
  get_filename_component(_abs_file ${file} ABSOLUTE)
  # /home/user/rippled/src/ripple/proto/org/.../v1
  get_filename_component(_abs_dir ${_abs_file} DIRECTORY)
  # get_ledger
  get_filename_component(_basename ${file} NAME_WE)
  # /home/user/rippled/src/ripple/proto
  get_filename_component(_proto_inc ${GRPC_PROTO_ROOT} DIRECTORY) # updir one level
  # org/.../v1/get_ledger.proto
  file(RELATIVE_PATH _rel_root_file ${_proto_inc} ${_abs_file})
  # org/.../v1
  get_filename_component(_rel_root_dir ${_rel_root_file} DIRECTORY)
  # src/ripple/proto/org/.../v1
  file(RELATIVE_PATH _rel_dir ${CMAKE_CURRENT_SOURCE_DIR} ${_abs_dir})

  # .cmake/proto_gen_grpc/ripple/proto/org/.../v1/get_ledger.grpc.pb.cc
  set(src_1 "${GRPC_GEN_DIR}/${_rel_root_dir}/${_basename}.grpc.pb.cc")
  set(src_2 "${GRPC_GEN_DIR}/${_rel_root_dir}/${_basename}.pb.cc")
  set(hdr_1 "${GRPC_GEN_DIR}/${_rel_root_dir}/${_basename}.grpc.pb.h")
  set(hdr_2 "${GRPC_GEN_DIR}/${_rel_root_dir}/${_basename}.pb.h")
  add_custom_command(
    OUTPUT ${src_1} ${src_2} ${hdr_1} ${hdr_2}
    COMMAND protobuf::protoc
    ARGS --grpc_out=${GRPC_GEN_DIR}
         --cpp_out=${GRPC_GEN_DIR}
         --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
         -I ${_proto_inc} -I ${_rel_dir}
         ${_abs_file}
    DEPENDS ${_abs_file} protobuf::protoc gRPC::grpc_cpp_plugin
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running gRPC C++ protocol buffer compiler on ${file}"
    VERBATIM)
    set_source_files_properties(${src_1} ${src_2} ${hdr_1} ${hdr_2} PROPERTIES
      GENERATED TRUE
      SKIP_UNITY_BUILD_INCLUSION ON
    )
    list(APPEND GRPC_PROTO_SRCS ${src_1} ${src_2})
    list(APPEND GRPC_PROTO_HDRS ${hdr_1} ${hdr_2})
endforeach()

target_include_directories(xrpl_core SYSTEM PUBLIC
  $<BUILD_INTERFACE:${output_dir}>
  $<BUILD_INTERFACE:${output_dir}/ripple/proto>
  # The generated sources include headers relative to this path. Fix it later.
  $<INSTALL_INTERFACE:include/ripple/proto>
)
target_sources(xrpl_core PRIVATE ${GRPC_PROTO_SRCS})
install(
  DIRECTORY ${output_dir}/ripple
  DESTINATION include/
  FILES_MATCHING PATTERN "*.h"
)
target_link_libraries(xrpl_core PUBLIC
  "gRPC::grpc++"
  # libgrpc is missing references.
  absl::random_random
)
target_compile_options(xrpl_core
  PRIVATE
    $<$<BOOL:${MSVC}>:-wd4065>
    $<$<NOT:$<BOOL:${MSVC}>>:-Wno-deprecated-declarations>
  PUBLIC
    $<$<BOOL:${MSVC}>:-wd4996>
    $<$<BOOL:${XCODE}>:
      --system-header-prefix="google/protobuf"
      -Wno-deprecated-dynamic-exception-spec
    >)
