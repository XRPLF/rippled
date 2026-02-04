#[===================================================================[
   declare options and variables
#]===================================================================]

include(CompilationEnv)

set(is_ci FALSE)
if (DEFINED ENV{CI})
    if ("$ENV{CI}" STREQUAL "true")
        set(is_ci TRUE)
    endif ()
endif ()

get_directory_property(has_parent PARENT_DIRECTORY)
if (has_parent)
    set(is_root_project OFF)
else ()
    set(is_root_project ON)
endif ()

option(assert "Enables asserts, even in release builds" OFF)

option(xrpld "Build xrpld" ON)

option(tests "Build tests" ON)
if (tests)
    # This setting allows making a separate workflow to test fees other than default 10
    if (NOT UNIT_TEST_REFERENCE_FEE)
        set(UNIT_TEST_REFERENCE_FEE "10" CACHE STRING "")
    endif ()
endif ()

option(unity "Creates a build using UNITY support in cmake." OFF)
if (unity)
    if (NOT is_ci)
        set(CMAKE_UNITY_BUILD_BATCH_SIZE 15 CACHE STRING "")
    endif ()
    set(CMAKE_UNITY_BUILD ON CACHE BOOL "Do a unity build")
endif ()

if (is_clang AND is_linux)
    option(voidstar "Enable Antithesis instrumentation." OFF)
endif ()

if (is_gcc OR is_clang)
    include(ProcessorCount)
    ProcessorCount(PROCESSOR_COUNT)

    option(coverage "Generates coverage info." OFF)
    option(profile "Add profiling flags" OFF)
    set(coverage_format "html-details" CACHE STRING "Output format of the coverage report.")
    set(coverage_extra_args "" CACHE STRING "Additional arguments to pass to gcovr.")
    option(wextra "compile with extra gcc/clang warnings enabled" ON)
else ()
    set(profile OFF CACHE BOOL "gcc/clang only" FORCE)
    set(coverage OFF CACHE BOOL "gcc/clang only" FORCE)
    set(wextra OFF CACHE BOOL "gcc/clang only" FORCE)
endif ()

if (is_linux AND NOT SANITIZER)
    option(BUILD_SHARED_LIBS "build shared xrpl libraries" OFF)
    option(static "link protobuf, openssl, libc++, and boost statically" ON)
    option(perf "Enables flags that assist with perf recording" OFF)
    option(use_gold "enables detection of gold (binutils) linker" ON)
    option(use_mold "enables detection of mold (binutils) linker" ON)
    # Set a default value for the log flag based on the build type. This provides a sensible default (on for debug, off
    # for release) while still allowing the user to override it for any build.
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(TRUNCATED_LOGS_DEFAULT ON)
    else ()
        set(TRUNCATED_LOGS_DEFAULT OFF)
    endif ()
    option(TRUNCATED_THREAD_NAME_LOGS "Show warnings about truncated thread names on Linux." ${TRUNCATED_LOGS_DEFAULT})
    if (TRUNCATED_THREAD_NAME_LOGS)
        add_compile_definitions(TRUNCATED_THREAD_NAME_LOGS)
    endif ()
else ()
    # we are not ready to allow shared-libs on windows because it would require export declarations. On macos it's more
    # feasible, but static openssl produces odd linker errors, thus we disable shared lib builds for now.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "build shared xrpl libraries - OFF for win/macos" FORCE)
    set(static ON CACHE BOOL "static link, linux only. ON for WIN/macos" FORCE)
    set(perf OFF CACHE BOOL "perf flags, linux only" FORCE)
    set(use_gold OFF CACHE BOOL "gold linker, linux only" FORCE)
    set(use_mold OFF CACHE BOOL "mold linker, linux only" FORCE)
endif ()

if (is_clang)
    option(use_lld "enables detection of lld linker" ON)
else ()
    set(use_lld OFF CACHE BOOL "try lld linker, clang only" FORCE)
endif ()

option(jemalloc "Enables jemalloc for heap profiling" OFF)
option(werr "treat warnings as errors" OFF)
option(local_protobuf "Force a local build of protobuf instead of looking for an installed version." OFF)
option(local_grpc "Force a local build of gRPC instead of looking for an installed version." OFF)

# the remaining options are obscure and rarely used
option(beast_no_unit_test_inline "Prevents unit test definitions from being inserted into global table" OFF)
option(single_io_service_thread "Restricts the number of threads calling io_context::run to one. \
  This can be useful when debugging." OFF)
option(boost_show_deprecated "Allow boost to fail on deprecated usage. Only useful if you're trying\
  to find deprecated calls." OFF)

if (WIN32)
    option(beast_disable_autolink "Disables autolinking of system libraries on WIN32" OFF)
else ()
    set(beast_disable_autolink OFF CACHE BOOL "WIN32 only" FORCE)
endif ()

if (coverage)
    message(STATUS "coverage build requested - forcing Debug build")
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "build type" FORCE)
endif ()
