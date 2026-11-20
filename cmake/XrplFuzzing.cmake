#[===================================================================[
   libFuzzer fuzz targets (-Dfuzzing=ON).

   Builds the xrpl_fuzz binary from src/test/fuzz. Every target in that
   directory is linked into the one binary; XRPL_FUZZ_TARGET selects which
   one libFuzzer drives.

   All project code is compiled with -fsanitize=fuzzer-no-link, which adds
   the coverage instrumentation libFuzzer needs without linking libFuzzer
   itself. Only xrpl_fuzz links -fsanitize=fuzzer, which supplies main().

   Sanitizers come from the usual SANITIZERS Conan profile setting. The
   coverage hooks are resolved by the sanitizer runtime in executables that
   do not link libFuzzer, so at least one sanitizer is required.
#]===================================================================]

include_guard(GLOBAL)
include(CompilationEnv)

if(NOT (is_llvm_clang AND is_linux))
    message(FATAL_ERROR "fuzzing requires LLVM Clang on Linux, aborting...")
endif()

# Antithesis adds its own coverage instrumentation; the two have not been tested together.
if(voidstar)
    message(FATAL_ERROR "fuzzing cannot be combined with voidstar, aborting...")
endif()

if(NOT SANITIZERS_ENABLED)
    message(
        FATAL_ERROR
        "fuzzing requires sanitizers; set SANITIZERS (e.g. address,undefinedbehavior) for Conan, aborting..."
    )
endif()

# XRPL_ASSERT is a plain assert(); fuzzing relies on it as a bug oracle.
if(NOT (assert OR CMAKE_BUILD_TYPE STREQUAL "Debug"))
    message(
        FATAL_ERROR
        "fuzzing requires asserts; use a Debug build or -Dassert=ON, aborting..."
    )
endif()

target_compile_options(
    common
    INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-fsanitize=fuzzer-no-link>
        $<$<COMPILE_LANGUAGE:C>:-fsanitize=fuzzer-no-link>
)

file(
    GLOB_RECURSE fuzz_sources
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/test/fuzz/*.cpp"
)
# The corpus replay unit test belongs to xrpld --unittest, not to the fuzzer.
list(FILTER fuzz_sources EXCLUDE REGEX "_test\\.cpp$")

add_executable(xrpl_fuzz ${fuzz_sources})
patch_nix_binary(xrpl_fuzz)
set_target_properties(
    xrpl_fuzz
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_include_directories(xrpl_fuzz PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(xrpl_fuzz PRIVATE xrpl.libxrpl)
target_link_options(xrpl_fuzz PRIVATE -fsanitize=fuzzer)
