#[===================================================================[
   Go Protobuf Bindings - Generate the Go gRPC client bindings

   The generated `.pb.go` files are committed to the source tree and consumed as
   a standalone Go module (`github.com/XRPLF/rippled/proto/org/xrpl/rpc/v1`);
   they are not compiled into xrpld. Regenerate them whenever the `.proto`
   definitions change by running:

     cmake --build . --target go_protobuf

   This target is excluded from ALL and is only available when `protoc` and the
   two Go plugins (`protoc-gen-go`, `protoc-gen-go-grpc`) are found on PATH.

   IMPORTANT: protoc embeds its own version string into every generated file,
   and CI verifies the committed files are up-to-date with a plain `git diff`.
   The toolchain must therefore be the version pinned in the Nix environment
   (protobuf_34 in nix/packages.nix), which provides all three tools:

     nix develop

   This deliberately does NOT use the Conan-provided `protobuf::protoc` used by
   the C++/gRPC build: that is a different protoc version, and the Go plugins
   are not part of the Conan toolchain. Keeping the whole Go toolchain pinned in
   one place (Nix) is what makes the output reproducible across developers and
   CI. Developers without Nix can install the tools manually, but must match the
   pinned versions or the CI check will flag the diff.
#]===================================================================]

# Directory holding the .proto files and the generated module.
set(GO_PROTO_IMPORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(GO_PROTO_OUT_DIR "${GO_PROTO_IMPORT_DIR}/org/xrpl/rpc/v1")

# Module path declared by `option go_package` in every .proto file. The
# --go_opt=module=<path> flag strips this prefix when computing output paths;
# since it equals the full go_package, the remaining relative path is empty and
# all generated files land directly in GO_PROTO_OUT_DIR.
set(GO_PROTO_MODULE "github.com/XRPLF/rippled/proto/org/xrpl/rpc/v1")

# The .proto files to generate Go bindings for, relative to the import dir.
file(
    GLOB_RECURSE GO_PROTO_FILES
    RELATIVE "${GO_PROTO_IMPORT_DIR}"
    "${GO_PROTO_IMPORT_DIR}/org/xrpl/rpc/v1/*.proto"
)

# Locate the toolchain on PATH. The GOPATH/bin hint helps non-Nix developers who
# installed the plugins with `go install`, which places them there (often off
# PATH). However, when not using the Nix-provided toolchain, there is a risk
# that the versions don't match and the generated files are different from CI.
find_program(GO_EXECUTABLE NAMES go)
set(GO_BIN_HINT "")
if(GO_EXECUTABLE)
    execute_process(
        COMMAND ${GO_EXECUTABLE} env GOPATH
        OUTPUT_VARIABLE GO_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(GO_PATH)
        set(GO_BIN_HINT "${GO_PATH}/bin")
    endif()
endif()

find_program(GO_PROTOC NAMES protoc)
find_program(PROTOC_GEN_GO NAMES protoc-gen-go HINTS ${GO_BIN_HINT})
find_program(PROTOC_GEN_GO_GRPC NAMES protoc-gen-go-grpc HINTS ${GO_BIN_HINT})

if(NOT GO_PROTOC OR NOT PROTOC_GEN_GO OR NOT PROTOC_GEN_GO_GRPC)
    message(
        STATUS
        "Go protobuf tooling not found; the 'go_protobuf' target will not be available.\n"
        "  protoc:             ${GO_PROTOC}\n"
        "  protoc-gen-go:      ${PROTOC_GEN_GO}\n"
        "  protoc-gen-go-grpc: ${PROTOC_GEN_GO_GRPC}\n"
        "Enter the Nix environment (nix develop) to get the pinned toolchain, "
        "or install the plugins manually with:\n"
        "  go install google.golang.org/protobuf/cmd/protoc-gen-go@latest\n"
        "  go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest"
    )
    return()
endif()

# Custom target to regenerate the committed Go bindings, excluded from ALL.
# Run manually with: cmake --build . --target go_protobuf
add_custom_target(
    go_protobuf
    COMMAND
        ${GO_PROTOC} --proto_path=${GO_PROTO_IMPORT_DIR}
        --plugin=protoc-gen-go=${PROTOC_GEN_GO}
        --plugin=protoc-gen-go-grpc=${PROTOC_GEN_GO_GRPC}
        --go_out=${GO_PROTO_OUT_DIR} --go_opt=module=${GO_PROTO_MODULE}
        --go-grpc_out=${GO_PROTO_OUT_DIR}
        --go-grpc_opt=module=${GO_PROTO_MODULE} ${GO_PROTO_FILES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT
        "Generating Go protobuf bindings (github.com/XRPLF/rippled/.../rpc/v1)..."
)
