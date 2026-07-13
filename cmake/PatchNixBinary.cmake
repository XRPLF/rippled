#[===================================================================[
   Patch executables to run in non-Nix environments.

   The Nix-based CI image links binaries against an ELF interpreter (loader)
   that lives in the Nix store, so the resulting binaries don't run elsewhere
   (including once installed from the .deb package). `patch_nix_binary` adds a
   POST_BUILD step that resets the interpreter to the system default loader and
   drops the rpath.

   This is only active inside the Nix-based image, detected by the presence of
   /tmp/loader-path.sh (shipped by that image, resolves the default loader). It
   is skipped for sanitizer builds, whose runtime libraries are resolved through
   the rpath. Everywhere else `patch_nix_binary` is a no-op.
#]===================================================================]

include_guard(GLOBAL)

include(CompilationEnv)

# Provided by the Nix-based CI image; prints the system default ELF loader path.
set(_loader_path_script "/tmp/loader-path.sh")

if(is_linux AND NOT SANITIZERS_ENABLED AND EXISTS "${_loader_path_script}")
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
