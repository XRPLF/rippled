#[===================================================================[
   declare user options/settings
#]===================================================================]

include(ProcessorCount)

ProcessorCount(PROCESSOR_COUNT)

option(assert "Enables asserts, even in release builds" OFF)

option(xrpld "Build xrpld" ON)

option(tests "Build tests" ON)
if(tests)
  # This setting allows making a separate workflow to test fees other than default 10
  if(NOT UNIT_TEST_REFERENCE_FEE)
    set(UNIT_TEST_REFERENCE_FEE "10" CACHE STRING "")
  endif()
endif()

option(unity "Creates a build using UNITY support in cmake." OFF)
if(unity)
  if(NOT is_ci)
    set(CMAKE_UNITY_BUILD_BATCH_SIZE 15 CACHE STRING "")
  endif()
  set(CMAKE_UNITY_BUILD ON CACHE BOOL "Do a unity build")
endif()
if(is_clang AND is_linux)
  option(voidstar "Enable Antithesis instrumentation." OFF)
endif()
if(is_gcc OR is_clang)
  option(coverage "Generates coverage info." OFF)
  option(profile "Add profiling flags" OFF)
  set(coverage_test_parallelism "${PROCESSOR_COUNT}" CACHE STRING
    "Unit tests parallelism for the purpose of coverage report.")
  set(coverage_format "html-details" CACHE STRING
    "Output format of the coverage report.")
  set(coverage_extra_args "" CACHE STRING
    "Additional arguments to pass to gcovr.")
  set(coverage_test "" CACHE STRING
    "On gcc & clang, the specific unit test(s) to run for coverage. Default is all tests.")
  if(coverage_test AND NOT coverage)
    set(coverage ON CACHE BOOL "gcc/clang only" FORCE)
  endif()
  option(wextra "compile with extra gcc/clang warnings enabled" ON)
else()
  set(profile OFF CACHE BOOL "gcc/clang only" FORCE)
  set(coverage OFF CACHE BOOL "gcc/clang only" FORCE)
  set(wextra OFF CACHE BOOL "gcc/clang only" FORCE)
endif()
if(is_linux)
  option(BUILD_SHARED_LIBS "build shared ripple libraries" OFF)
  option(static "link protobuf, openssl, libc++, and boost statically" ON)
  option(perf "Enables flags that assist with perf recording" OFF)
  option(use_gold "enables detection of gold (binutils) linker" ON)
  option(use_mold "enables detection of mold (binutils) linker" ON)
else()
  # we are not ready to allow shared-libs on windows because it would require
  # export declarations. On macos it's more feasible, but static openssl
  # produces odd linker errors, thus we disable shared lib builds for now.
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "build shared ripple libraries - OFF for win/macos" FORCE)
  set(static ON CACHE BOOL "static link, linux only. ON for WIN/macos" FORCE)
  set(perf OFF CACHE BOOL "perf flags, linux only" FORCE)
  set(use_gold OFF CACHE BOOL "gold linker, linux only" FORCE)
  set(use_mold OFF CACHE BOOL "mold linker, linux only" FORCE)
endif()
if(is_clang)
  option(use_lld "enables detection of lld linker" ON)
else()
  set(use_lld OFF CACHE BOOL "try lld linker, clang only" FORCE)
endif()
option(jemalloc "Enables jemalloc for heap profiling" OFF)
option(werr "treat warnings as errors" OFF)
option(local_protobuf
  "Force a local build of protobuf instead of looking for an installed version." OFF)
option(local_grpc
  "Force a local build of gRPC instead of looking for an installed version." OFF)

# this one is a string and therefore can't be an option
set(san "" CACHE STRING "On gcc & clang, add sanitizer instrumentation")
set_property(CACHE san PROPERTY STRINGS ";undefined;memory;address;thread")
if(san)
  string(TOLOWER ${san} san)
  set(SAN_FLAG "-fsanitize=${san}")
  set(SAN_LIB "")
  if(is_gcc)
    if(san STREQUAL "address")
      set(SAN_LIB "asan")
    elseif(san STREQUAL "thread")
      set(SAN_LIB "tsan")
    elseif(san STREQUAL "memory")
      set(SAN_LIB "msan")
    elseif(san STREQUAL "undefined")
      set(SAN_LIB "ubsan")
    endif()
  endif()
  set(_saved_CRL ${CMAKE_REQUIRED_LIBRARIES})
  set(CMAKE_REQUIRED_LIBRARIES "${SAN_FLAG};${SAN_LIB}")
  check_cxx_compiler_flag(${SAN_FLAG} COMPILER_SUPPORTS_SAN)
  set(CMAKE_REQUIRED_LIBRARIES ${_saved_CRL})
  if(NOT COMPILER_SUPPORTS_SAN)
    message(FATAL_ERROR "${san} sanitizer does not seem to be supported by your compiler")
  endif()
endif()
set(container_label "" CACHE STRING "tag to use for package building containers")
option(packages_only
  "ONLY generate package building targets. This is special use-case and almost \
   certainly not what you want. Use with caution as you won't be able to build \
   any compiled targets locally." OFF)
option(have_package_container
  "Sometimes you already have the tagged container you want to use for package \
   building and you don't want docker to rebuild it. This flag will detach the \
   dependency of the package build from the container build. It's an advanced \
   use case and most likely you should not be touching this flag." OFF)

# the remaining options are obscure and rarely used
option(beast_no_unit_test_inline
  "Prevents unit test definitions from being inserted into global table"
  OFF)
option(single_io_service_thread
  "Restricts the number of threads calling io_service::run to one. \
  This can be useful when debugging."
  OFF)
option(boost_show_deprecated
  "Allow boost to fail on deprecated usage. Only useful if you're trying\
  to find deprecated calls."
  OFF)
option(beast_hashers
  "Use local implementations for sha/ripemd hashes (experimental, not recommended)"
  OFF)

if(WIN32)
  option(beast_disable_autolink "Disables autolinking of system libraries on WIN32" OFF)
else()
  set(beast_disable_autolink OFF CACHE BOOL "WIN32 only" FORCE)
endif()
if(coverage)
  message(STATUS "coverage build requested - forcing Debug build")
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "build type" FORCE)
endif()
