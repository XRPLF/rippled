# Custom CMake command definitions for gersemi formatting.
# These stubs teach gersemi the signatures of project-specific commands
# so it can format their invocations correctly.

function(git_branch branch_val)
endfunction()

function(isolate_headers target A B scope)
endfunction()

function(create_symbolic_link target link)
endfunction()

function(xrpl_add_benchmark name)
endfunction()

macro(exclude_from_default target_)
endmacro()

macro(exclude_if_included target_)
endmacro()

function(target_protobuf_sources target prefix)
    set(options APPEND_PATH DESCRIPTORS)
    set(oneValueArgs
        LANGUAGE
        OUT_VAR
        EXPORT_MACRO
        TARGET
        PROTOC_OUT_DIR
        PLUGIN
        PLUGIN_OPTIONS
        PROTOC_EXE
    )
    set(multiValueArgs
        PROTOS
        IMPORT_DIRS
        GENERATE_EXTENSIONS
        PROTOC_OPTIONS
        DEPENDENCIES
    )
    cmake_parse_arguments(
        THIS_FUNCTION_PREFIX
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )
endfunction()

function(add_module parent name)
endfunction()

function(verify_target_headers target headers_dir)
endfunction()

function(_verify_add_headers target dir)
endfunction()

function(setup_protocol_autogen)
endfunction()

function(target_link_modules parent scope)
endfunction()

function(setup_target_for_coverage_gcovr)
    set(options NONE)
    set(oneValueArgs BASE_DIRECTORY NAME FORMAT)
    set(multiValueArgs EXCLUDE EXECUTABLE EXECUTABLE_ARGS DEPENDENCIES)
    cmake_parse_arguments(
        THIS_FUNCTION_PREFIX
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )
endfunction()

function(add_code_coverage_to_target name scope)
endfunction()

function(verbose_find_path variable name)
    set(options
        NO_CACHE
        REQUIRED
        OPTIONAL
        NO_DEFAULT_PATH
        NO_PACKAGE_ROOT_PATH
        NO_CMAKE_PATH
        NO_CMAKE_ENVIRONMENT_PATH
        NO_SYSTEM_ENVIRONMENT_PATH
        NO_CMAKE_SYSTEM_PATH
        NO_CMAKE_INSTALL_PREFIX
        CMAKE_FIND_ROOT_PATH_BOTH
        ONLY_CMAKE_FIND_ROOT_PATH
        NO_CMAKE_FIND_ROOT_PATH
    )
    set(oneValueArgs REGISTRY_VIEW VALIDATOR DOC)
    set(multiValueArgs NAMES HINTS PATHS PATH_SUFFIXES)
    cmake_parse_arguments(
        THIS_FUNCTION_PREFIX
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )
endfunction()

function(patch_nix_binary target)
endfunction()

function(rpcspec_generate_instantiations)
    set(options)
    set(oneValueArgs OUT_VAR VALUE_TYPE VIEW_HEADER INCLUDE_DIR)
    set(multiValueArgs HANDLERS)
    cmake_parse_arguments(
        THIS_FUNCTION_PREFIX
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )
endfunction()

function(corrosion_import_crate)
    set(options
        ALL_FEATURES
        NO_DEFAULT_FEATURES
        NO_STD
        NO_LINKER_OVERRIDE
        NO_USES_TERMINAL
        LOCKED
        FROZEN
    )
    set(oneValueArgs MANIFEST_PATH PROFILE IMPORTED_CRATES)
    set(multiValueArgs
        CRATE_TYPES
        CRATES
        FEATURES
        FLAGS
        OVERRIDE_CRATE_TYPE
    )
    cmake_parse_arguments(
        THIS_FUNCTION_PREFIX
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )
endfunction()

function(corrosion_set_env_vars target_name env_var)
endfunction()

function(corrosion_add_cxxbridge cxx_target)
    set(options)
    set(oneValueArgs CRATE)
    set(multiValueArgs FILES)
    cmake_parse_arguments(
        THIS_FUNCTION_PREFIX
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )
endfunction()

function(_unlink_libgcc_s crate)
endfunction()

function(add_xrpl_crate name)
    set(options)
    set(oneValueArgs CRATE)
    set(multiValueArgs FILES)
    cmake_parse_arguments(
        THIS_FUNCTION_PREFIX
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )
endfunction()
