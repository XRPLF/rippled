#[===================================================================[
   Patch executables to run in non-Nix environments.

   The Nix toolchain links binaries against an ELF interpreter (loader)
   that lives in the Nix store, so the resulting binaries don't run elsewhere.
   `patch_nix_binary` adds a POST_BUILD step that resets the interpreter
   to the system default loader and drops the rpath.

   This runs by default for Nix-toolchain builds (determined by whether the compiler resolves under /nix/store/).
   Those builds are where binaries get a Nix-store loader.
   It is opted out of by setting the XRPLD_NO_PATCH_NIX_BINARY environment variable —
   the plain Nix dev shells set it, since their binaries link a newer glibc
   and must not be retargeted to the system loader.

   Non-Nix builds (a system compiler, already using the system loader) and sanitizer builds
   (runtime libraries resolved through the rpath) are skipped too.
   Everywhere else `patch_nix_binary` is a no-op.

   The default loader is resolved by bin/default-loader-path.sh.
#]===================================================================]

include_guard(GLOBAL)

include(CompilationEnv)

# Resolves the system default ELF loader path for the current architecture.
set(_loader_path_script "${CMAKE_SOURCE_DIR}/bin/default-loader-path.sh")

if(
    is_linux
    AND NOT SANITIZERS_ENABLED
    AND is_nix_compiler
    AND NOT DEFINED ENV{XRPLD_NO_PATCH_NIX_BINARY}
)
    execute_process(
        COMMAND "${_loader_path_script}"
        OUTPUT_VARIABLE DEFAULT_LOADER_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    find_program(PATCHELF_COMMAND patchelf REQUIRED)
    set(PATCH_NIX_BINARIES TRUE)
    message(
        STATUS
        "Binaries will be patched to use loader '${DEFAULT_LOADER_PATH}'"
    )
else()
    set(PATCH_NIX_BINARIES FALSE)
endif()

function(patch_nix_binary target)
    if(NOT PATCH_NIX_BINARIES)
        return()
    endif()
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND
            "${PATCHELF_COMMAND}" --set-interpreter "${DEFAULT_LOADER_PATH}"
            --remove-rpath "$<TARGET_FILE:${target}>"
        COMMENT "Patching ${target}: set default loader, remove rpath"
        VERBATIM
    )
endfunction()
