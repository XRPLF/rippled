
cmake_minimum_required (VERSION 3.9.0)
set (CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Builds/CMake ${CMAKE_MODULE_PATH})
include (CheckCXXCompilerFlag)
if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.11)
  include (FetchContent)
endif ()
if (MSVC AND CMAKE_VERSION VERSION_LESS 3.12)
  message (FATAL_ERROR "MSVC requires cmake 3.12 or greater for proper boost support")
endif ()
include (ExternalProject)
include (CMakeFuncs) # must come *after* ExternalProject b/c it overrides one function in EP
include (ProcessorCount)
if (target)
  message (WARNING
    "The target option is deprecated and will be removed in a future release")
  parse_target()
endif ()
project (rippled)

if (POLICY CMP0074)
  cmake_policy(SET CMP0074 NEW)
endif ()
#[===================================================================[
   convenience variables and sanity checks
#]===================================================================]

if (NOT ep_procs)
  ProcessorCount(ep_procs)
  if (ep_procs GREATER 1)
    # never use more than half of cores for EP builds
    math (EXPR ep_procs "${ep_procs} / 2")
    message (STATUS "Using ${ep_procs} cores for ExternalProject builds.")
  endif ()
endif ()
get_property (is_multiconfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (is_multiconfig STREQUAL "NOTFOUND")
  if (${CMAKE_GENERATOR} STREQUAL "Xcode" OR ${CMAKE_GENERATOR} MATCHES "^Visual Studio")
    set (is_multiconfig TRUE)
  endif ()
endif ()

set (CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
if (NOT is_multiconfig)
  if (NOT CMAKE_BUILD_TYPE)
    message (STATUS "Build type not specified - defaulting to Release")
    set (CMAKE_BUILD_TYPE Release CACHE STRING "build type" FORCE)
  elseif (NOT (CMAKE_BUILD_TYPE STREQUAL Debug OR CMAKE_BUILD_TYPE STREQUAL Release))
    # for simplicity, these are the only two config types we care about. Limiting
    # the build types simplifies dealing with external project builds especially
    message (FATAL_ERROR " *** Only Debug or Release build types are currently supported ***")
  endif ()
endif ()

get_directory_property(has_parent PARENT_DIRECTORY)
if (has_parent)
  set (is_root_project OFF)
else ()
  set (is_root_project ON)
endif ()

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES ".*Clang") # both Clang and AppleClang
  set (is_clang TRUE)
  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" AND
         CMAKE_CXX_COMPILER_VERSION VERSION_LESS 5.0)
    message (FATAL_ERROR "This project requires clang 5 or later")
  endif ()
  # TODO min AppleClang version check ?
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set (is_gcc TRUE)
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.0)
    message (FATAL_ERROR "This project requires GCC 7 or later")
  endif ()
endif ()
if (CMAKE_GENERATOR STREQUAL "Xcode")
  set (is_xcode TRUE)
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set (is_linux TRUE)
else ()
  set (is_linux FALSE)
endif ()

if ("$ENV{CI}" STREQUAL "true" OR "$ENV{CONTINUOUS_INTEGRATION}" STREQUAL "true")
  set (is_ci TRUE)
else ()
  set (is_ci FALSE)
endif ()

# check for in-source build and fail
if ("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
  message (FATAL_ERROR "Builds (in-source) are not allowed in "
    "${CMAKE_CURRENT_SOURCE_DIR}. Please remove CMakeCache.txt and the CMakeFiles "
    "directory from ${CMAKE_CURRENT_SOURCE_DIR} and try building in a separate directory.")
endif ()

if ("${CMAKE_GENERATOR}" MATCHES "Visual Studio" AND
    NOT ("${CMAKE_GENERATOR}" MATCHES .*Win64.*))
  message (FATAL_ERROR
    "Visual Studio 32-bit build is not supported. Use -G\"${CMAKE_GENERATOR} Win64\"")
endif ()

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message (FATAL_ERROR "Rippled requires a 64 bit target architecture.\n"
    "The most likely cause of this warning is trying to build rippled with a 32-bit OS.")
endif ()

if (APPLE AND NOT HOMEBREW)
  find_program (HOMEBREW brew)
endif ()

#[===================================================================[
   read version from source
#]===================================================================]

file (STRINGS src/ripple/protocol/impl/BuildInfo.cpp BUILD_INFO)
foreach (line_ ${BUILD_INFO})
  if (line_ MATCHES "versionString[ ]*=[ ]*\"(.+)\"")
    set (rippled_version ${CMAKE_MATCH_1})
  endif ()
endforeach ()
if (rippled_version)
  message (STATUS "rippled version: ${rippled_version}")
else ()
  message (FATAL_ERROR "unable to determine rippled version")
endif ()

#[===================================================================[
   declare user options/settings
#]===================================================================]

option (assert "Enables asserts, even in release builds" OFF)
option (unity "Creates a build based on unity sources. This is the default" ON)
if (is_gcc OR is_clang)
  option (coverage "Generates coverage info." OFF)
  option (profile "Add profiling flags" OFF)
  set (coverage_test "" CACHE STRING
    "On gcc & clang, the specific unit test(s) to run for coverage. Default is all tests.")
  if (coverage_test AND NOT coverage)
    set (coverage ON CACHE BOOL "gcc/clang only" FORCE)
  endif ()
  option (coverage_core_only
    "Include only src/ripple files when generating coverage report. \
    Set to OFF to include all sources in coverage report."
    ON)
else ()
  set (profile OFF CACHE BOOL "gcc/clang only" FORCE)
  set (coverage OFF CACHE BOOL "gcc/clang only" FORCE)
endif ()
if (is_linux)
  option (BUILD_SHARED_LIBS "build shared ripple libraries" OFF)
  option (static "link protobuf, openssl, libc++, and boost statically" ON)
  option (perf "Enables flags that assist with perf recording" OFF)
  option (use_gold "enables detection of gold (binutils) linker" ON)
else ()
  # we are not ready to allow shared-libs on windows because it would require
  # export declarations. On macos it's more feasible, but static openssl
  # produces odd linker errors, thus we disable shared lib builds for now.
  set (BUILD_SHARED_LIBS OFF CACHE BOOL "build shared ripple libraries - OFF for win/macos" FORCE)
  set (static ON CACHE BOOL "static link, linux only. ON for WIN/macos" FORCE)
  set (perf OFF CACHE BOOL "perf flags, linux only" FORCE)
  set (use_gold OFF CACHE BOOL "gold linker, linux only" FORCE)
endif ()
if (is_clang)
  option (use_lld "enables detection of lld linker" ON)
else ()
  set (use_lld OFF CACHE BOOL "try lld linker, clang only" FORCE)
endif ()
option (jemalloc "Enables jemalloc for heap profiling" OFF)
option (werr "treat warnings as errors" OFF)
option (local_protobuf
  "Force use of a local build of protobuf instead of system version." OFF)

# this one is a string and therefore can't be an option
set (san "" CACHE STRING "On gcc & clang, add sanitizer instrumentation")
set_property (CACHE san PROPERTY STRINGS ";undefined;memory;address;thread")
if (san)
  string (TOLOWER ${san} san)
  set (SAN_FLAG "-fsanitize=${san}")
  set (SAN_LIB "")
  if (is_gcc)
    if (san STREQUAL "address")
      set (SAN_LIB "asan")
    elseif (san STREQUAL "thread")
      set (SAN_LIB "tsan")
    elseif (san STREQUAL "memory")
      set (SAN_LIB "msan")
    elseif (san STREQUAL "undefined")
      set (SAN_LIB "ubsan")
    endif ()
  endif ()
  set (_saved_CRL ${CMAKE_REQUIRED_LIBRARIES})
  set (CMAKE_REQUIRED_LIBRARIES "${SAN_FLAG};${SAN_LIB}")
  check_cxx_compiler_flag (${SAN_FLAG} COMPILER_SUPPORTS_SAN)
  set (CMAKE_REQUIRED_LIBRARIES ${_saved_CRL})
  if (NOT COMPILER_SUPPORTS_SAN)
    message (FATAL_ERROR "${san} sanitizer does not seem to be supported by your compiler")
  endif ()
endif ()
set (container_label "" CACHE STRING "tag to use for package building containers")
option (packages_only
  "ONLY generate package building targets. This is special use-case and almost \
   certainly not what you want. Use with caution as you won't be able to build \
   any compiled targets locally." OFF)
option (have_package_container
  "Sometimes you already have the tagged container you want to use for package \
   building and you don't want docker to rebuild it. This flag will detach the \
   dependency of the package build from the container build. It's an advanced \
   use case and most likely you should not be touching this flag." OFF)

# the remaining options are obscure and rarely used
option (beast_no_unit_test_inline
  "Prevents unit test definitions from being inserted into global table"
  OFF)
# NOTE - THIS OPTION CURRENTLY DOES NOT COMPILE :
# TODO: fix or remove
option (verify_nodeobject_keys
  "This verifies that the hash of node objects matches the payload. \
  This check is expensive - use with caution."
  OFF)
option (single_io_service_thread
  "Restricts the number of threads calling io_service::run to one. \
  This can be useful when debugging."
  OFF)
option (boost_show_deprecated
  "Allow boost to fail on deprecated usage. Only useful if you're trying\
  to find deprecated calls."
  OFF)
option (beast_hashers
  "Use local implementations for sha/ripemd hashes (experimental, not recommended)"
  OFF)

if (WIN32)
  option (beast_disable_autolink "Disables autolinking of system libraries on WIN32" OFF)
else ()
  set (beast_disable_autolink OFF CACHE BOOL "WIN32 only" FORCE)
endif ()
if (coverage)
  message (STATUS "coverage build requested - forcing Debug build")
  set (CMAKE_BUILD_TYPE Debug CACHE STRING "build type" FORCE)
endif ()

#[===================================================================[
   NIH prefix path..this is where we will download
   and build any ExternalProjects, and they will hopefully
   survive across build directory deletion (manual cleans)
#]===================================================================]

string (REGEX REPLACE "[ \\/%]+" "_" gen_for_path ${CMAKE_GENERATOR})
string (TOLOWER ${gen_for_path} gen_for_path)
# HACK: trying to shorten paths for windows CI (which hits 260 MAXPATH easily)
# @see:  https://issues.jenkins-ci.org/browse/JENKINS-38706?focusedCommentId=339847
string (REPLACE "visual_studio" "vs" gen_for_path ${gen_for_path})
if (NOT DEFINED NIH_CACHE_ROOT)
  if (DEFINED ENV{NIH_CACHE_ROOT})
    set (NIH_CACHE_ROOT $ENV{NIH_CACHE_ROOT})
  else ()
    set (NIH_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.nih_c")
  endif ()
endif ()
set (nih_cache_path
  "${NIH_CACHE_ROOT}/${gen_for_path}/${CMAKE_CXX_COMPILER_ID}_${CMAKE_CXX_COMPILER_VERSION}")
if (NOT is_multiconfig)
  set (nih_cache_path "${nih_cache_path}/${CMAKE_BUILD_TYPE}")
endif ()
file(TO_CMAKE_PATH "${nih_cache_path}" nih_cache_path)
message (STATUS "NIH-EP cache path: ${nih_cache_path}")
## two convenience variables:
set (ep_lib_prefix ${CMAKE_STATIC_LIBRARY_PREFIX})
set (ep_lib_suffix ${CMAKE_STATIC_LIBRARY_SUFFIX})

# this is a setting for FetchContent and needs to be
# a cache variable
# https://cmake.org/cmake/help/latest/module/FetchContent.html#populating-the-content
set (FETCHCONTENT_BASE_DIR ${nih_cache_path} CACHE STRING "" FORCE)

#[===================================================================[
   package/container targets - (optional)
#]===================================================================]

if (is_root_project)
  if (NOT DOCKER)
    find_program (DOCKER docker)
  endif ()

  if (DOCKER)
    # if no container label is provided, use current git hash
    git_hash (commit_hash)
    if (NOT container_label)
      set (container_label ${commit_hash})
    endif ()
    message (STATUS "using [${container_label}] as build container tag...")

    file (MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/packages)
    file (MAKE_DIRECTORY ${NIH_CACHE_ROOT}/pkgbuild)
    if (is_linux)
      execute_process (COMMAND id -u
        OUTPUT_VARIABLE DOCKER_USER_ID
        OUTPUT_STRIP_TRAILING_WHITESPACE)
      message (STATUS "docker local user id: ${DOCKER_USER_ID}")
      execute_process (COMMAND id -g
        OUTPUT_VARIABLE DOCKER_GROUP_ID
        OUTPUT_STRIP_TRAILING_WHITESPACE)
      message (STATUS "docker local group id: ${DOCKER_GROUP_ID}")
    endif ()
    if (DOCKER_USER_ID AND DOCKER_GROUP_ID)
      set(map_user TRUE)
    endif ()
    #[===================================================================[
        rpm
    #]===================================================================]
    add_custom_target (rpm_container
      docker build
        --pull
        --build-arg GIT_COMMIT=${commit_hash}
        -t rippled-rpm-builder:${container_label}
        $<$<BOOL:${rpm_cache_from}>:--cache-from=${rpm_cache_from}>
        -f centos-builder/Dockerfile .
      WORKING_DIRECTORY  ${CMAKE_CURRENT_SOURCE_DIR}/Builds/containers
      VERBATIM
      USES_TERMINAL
      COMMAND_EXPAND_LISTS
      SOURCES
        Builds/containers/centos-builder/Dockerfile
        Builds/containers/centos-builder/centos_setup.sh
        Builds/containers/centos-builder/extras.sh
        Builds/containers/shared/build_deps.sh
        Builds/containers/shared/rippled.service
        Builds/containers/shared/update_sources.sh
        Builds/containers/shared/update-rippled.sh
        Builds/containers/packaging/rpm/rippled.spec
        Builds/containers/packaging/rpm/build_rpm.sh
    )
    exclude_from_default (rpm_container)
    add_custom_target (rpm
      docker run
        -e NIH_CACHE_ROOT=/opt/rippled_bld/pkg/.nih_c
        -v ${NIH_CACHE_ROOT}/pkgbuild:/opt/rippled_bld/pkg/.nih_c
        -v ${CMAKE_SOURCE_DIR}:/opt/rippled_bld/pkg/rippled
        -v ${CMAKE_CURRENT_BINARY_DIR}/packages:/opt/rippled_bld/pkg/out
        "$<$<BOOL:${map_user}>:--volume=/etc/passwd:/etc/passwd;--volume=/etc/group:/etc/group;--user=${DOCKER_USER_ID}:${DOCKER_GROUP_ID}>"
        -t rippled-rpm-builder:${container_label}
      VERBATIM
      USES_TERMINAL
      COMMAND_EXPAND_LISTS
      SOURCES
        Builds/containers/packaging/rpm/rippled.spec
    )
    exclude_from_default (rpm)
    if (NOT have_package_container)
      add_dependencies(rpm rpm_container)
    endif ()
    #[===================================================================[
        dpkg
    #]===================================================================]
    # currently use ubuntu 16.04 as a base b/c it has one of
    # the lower versions of libc among ubuntu and debian releases.
    # we could change this in the future and build with some other deb
    # based system.
    add_custom_target (dpkg_container
      docker build
        --pull
        --build-arg DIST_TAG=16.04
        --build-arg GIT_COMMIT=${commit_hash}
        -t rippled-dpkg-builder:${container_label}
        $<$<BOOL:${dpkg_cache_from}>:--cache-from=${dpkg_cache_from}>
        -f ubuntu-builder/Dockerfile .
      WORKING_DIRECTORY  ${CMAKE_CURRENT_SOURCE_DIR}/Builds/containers
      VERBATIM
      USES_TERMINAL
      COMMAND_EXPAND_LISTS
      SOURCES
        Builds/containers/ubuntu-builder/Dockerfile
        Builds/containers/ubuntu-builder/ubuntu_setup.sh
        Builds/containers/shared/build_deps.sh
        Builds/containers/shared/rippled.service
        Builds/containers/shared/update_sources.sh
        Builds/containers/shared/update-rippled.sh
        Builds/containers/packaging/dpkg/build_dpkg.sh
        Builds/containers/packaging/dpkg/debian/README.Debian
        Builds/containers/packaging/dpkg/debian/conffiles
        Builds/containers/packaging/dpkg/debian/control
        Builds/containers/packaging/dpkg/debian/copyright
        Builds/containers/packaging/dpkg/debian/dirs
        Builds/containers/packaging/dpkg/debian/docs
        Builds/containers/packaging/dpkg/debian/rippled-dev.install
        Builds/containers/packaging/dpkg/debian/rippled.install
        Builds/containers/packaging/dpkg/debian/rippled.links
        Builds/containers/packaging/dpkg/debian/rippled.postinst
        Builds/containers/packaging/dpkg/debian/rippled.postrm
        Builds/containers/packaging/dpkg/debian/rippled.preinst
        Builds/containers/packaging/dpkg/debian/rippled.prerm
        Builds/containers/packaging/dpkg/debian/rules
    )
    exclude_from_default (dpkg_container)
    add_custom_target (dpkg
      docker run
        -e NIH_CACHE_ROOT=/opt/rippled_bld/pkg/.nih_c
        -v ${NIH_CACHE_ROOT}/pkgbuild:/opt/rippled_bld/pkg/.nih_c
        -v ${CMAKE_SOURCE_DIR}:/opt/rippled_bld/pkg/rippled
        -v ${CMAKE_CURRENT_BINARY_DIR}/packages:/opt/rippled_bld/pkg/out
        "$<$<BOOL:${map_user}>:--volume=/etc/passwd:/etc/passwd;--volume=/etc/group:/etc/group;--user=${DOCKER_USER_ID}:${DOCKER_GROUP_ID}>"
        -t rippled-dpkg-builder:${container_label}
      VERBATIM
      USES_TERMINAL
      COMMAND_EXPAND_LISTS
      SOURCES
        Builds/containers/packaging/dpkg/debian/control
    )
    exclude_from_default (dpkg)
    if (NOT have_package_container)
      add_dependencies(dpkg dpkg_container)
    endif ()
    #[===================================================================[
        ci container
    #]===================================================================]
    # now use the same ubuntu image for our travis-ci docker images,
    # but we use a newer distro (18.04 vs 16.04).
    #
    # steps for publishing a new CI image when you make changes:
    #
    #   mkdir bld.ci && cd bld.ci && cmake -Dpackages_only=ON -Dcontainer_label=CI_LATEST
    #   cmake --build . --target ci_container --verbose
    #   docker tag rippled-ci-builder:CI_LATEST <DOCKERHUB_USER>/rippled-ci-builder:YYYY-MM-DD
    #       (change YYYY-MM-DD to match current date..or use a different
    #        tag/label scheme if you prefer)
    #   docker push <DOCKERHUB_USER>/rippled-ci-builder:YYYY-MM-DD
    #
    # ...then change the DOCKER_IMAGE line in .travis.yml :
    #     - DOCKER_IMAGE="<DOCKERHUB_USER>/rippled-ci-builder:YYYY-MM-DD"
    add_custom_target (ci_container
      docker build
        --pull
        --build-arg DIST_TAG=18.04
        --build-arg GIT_COMMIT=${commit_hash}
        --build-arg CI_USE=true
        -t rippled-ci-builder:${container_label}
        $<$<BOOL:${ci_cache_from}>:--cache-from=${ci_cache_from}>
        -f ubuntu-builder/Dockerfile .
      WORKING_DIRECTORY  ${CMAKE_CURRENT_SOURCE_DIR}/Builds/containers
      VERBATIM
      USES_TERMINAL
      COMMAND_EXPAND_LISTS
      SOURCES
        Builds/containers/ubuntu-builder/Dockerfile
        Builds/containers/ubuntu-builder/ubuntu_setup.sh
        Builds/containers/shared/build_deps.sh
        Builds/containers/shared/rippled.service
        Builds/containers/shared/update_sources.sh
        Builds/containers/shared/update-rippled.sh
    )
    exclude_from_default (ci_container)
  else ()
    message (STATUS "docker NOT found -- won't be able to build containers for packaging")
  endif ()
endif ()

if (packages_only)
  if (NOT TARGET rpm)
    message (FATAL_ERROR "packages_only requested, but targets were not created - is docker installed?")
  endif()
  return ()
endif ()

#[===================================================================[
   setup project-wide compiler settings
#]===================================================================]

#[=========================================================[
   TODO some/most of these common settings belong in a
   toolchain file, especially the ABI-impacting ones
#]=========================================================]
add_library (common INTERFACE)
add_library (Ripple::common ALIAS common)
# add a single global dependency on this interface lib
link_libraries (Ripple::common)
set_target_properties (common
  PROPERTIES INTERFACE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_EXTENSIONS OFF)
target_compile_features (common INTERFACE cxx_std_17)
target_compile_definitions (common
  INTERFACE
    $<$<CONFIG:Debug>:DEBUG _DEBUG>
    $<$<AND:$<BOOL:${profile}>,$<NOT:$<BOOL:${assert}>>>:NDEBUG>)
    # ^^^^ NOTE: CMAKE release builds already have NDEBUG
    # defined, so no need to add it explicitly except for
    # this special case of (profile ON) and (assert OFF)
    # -- presumably this is because we don't want profile
    # builds asserting unless asserts were specifically
    # requested

if (MSVC)
  # remove existing exception flag since we set it to -EHa
  string (REGEX REPLACE "[-/]EH[a-z]+" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  # also remove dynamic runtime
  foreach (var_
      CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
      CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE)
    string (REGEX REPLACE "[-/]MD[d]*" " " ${var_} "${${var_}}")
  endforeach ()

  target_compile_options (common
    INTERFACE
      -bigobj            # Increase object file max size
      -fp:precise        # Floating point behavior
      -Gd                # __cdecl calling convention
      -Gm-               # Minimal rebuild: disabled
      -Gy-               # Function level linking: disabled
      -MP                # Multiprocessor compilation
      -openmp-           # pragma omp: disabled
      -errorReport:none  # No error reporting to Internet
      -nologo            # Suppress login banner
      -wd4018            # Disable signed/unsigned comparison warnings
      -wd4244            # Disable float to int possible loss of data warnings
      -wd4267            # Disable size_t to T possible loss of data warnings
      -wd4800            # Disable C4800(int to bool performance)
      -wd4503            # Decorated name length exceeded, name was truncated
      $<$<COMPILE_LANGUAGE:CXX>:
        -EHa
        -GR
      >
      $<$<CONFIG:Release>:-Ox>
      $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:
        -GS
        -Zc:forScope
      >
      # static runtime
      $<$<CONFIG:Debug>:-MTd>
      $<$<NOT:$<CONFIG:Debug>>:-MT>
      $<$<BOOL:${werr}>:-WX>
      )
  target_compile_definitions (common
    INTERFACE
      _WIN32_WINNT=0x6000
      _SCL_SECURE_NO_WARNINGS
      _CRT_SECURE_NO_WARNINGS
      WIN32_CONSOLE
      NOMINMAX
      # TODO: Resolve these warnings, don't just silence them
      _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
      $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:_CRTDBG_MAP_ALLOC>)
  target_link_libraries (common
    INTERFACE
      -errorreport:none
      -machine:X64)
else ()
  # HACK : because these need to come first, before any warning demotion
  string (APPEND CMAKE_CXX_FLAGS " -Wall -Wdeprecated")
  # not MSVC
  target_compile_options (common
    INTERFACE
      $<$<BOOL:${werr}>:-Werror>
      $<$<COMPILE_LANGUAGE:CXX>:
        -frtti
        -Wnon-virtual-dtor
      >
      -Wno-sign-compare
      -Wno-char-subscripts
      -Wno-format
      -Wno-unused-local-typedefs
      $<$<BOOL:${is_gcc}>:
        -Wno-unused-but-set-variable
        -Wno-deprecated
      >
      $<$<NOT:$<CONFIG:Debug>>:-fno-strict-aliasing>
      # tweak gcc optimization for debug
      $<$<AND:$<BOOL:${is_gcc}>,$<CONFIG:Debug>>:-O0>
      # Add debug symbols to release config
      $<$<CONFIG:Release>:-g>)
  target_link_libraries (common
    INTERFACE
      -rdynamic
      # link to static libc/c++ iff:
      #   * static option set and
      #   * NOT APPLE (AppleClang does not support static libc/c++) and
      #   * NOT san (sanitizers typically don't work with static libc/c++)
      $<$<AND:$<BOOL:${static}>,$<NOT:$<BOOL:${APPLE}>>,$<NOT:$<BOOL:${san}>>>:-static-libstdc++>)
endif ()

if (use_gold AND is_gcc)
  # use gold linker if available
  execute_process (
    COMMAND ${CMAKE_CXX_COMPILER} -fuse-ld=gold -Wl,--version
    ERROR_QUIET OUTPUT_VARIABLE LD_VERSION)
    #[=========================================================[
       NOTE: THE gold linker inserts -rpath as DT_RUNPATH by
       default intead of DT_RPATH, so you might have slightly
       unexpected runtime ld behavior if you were expecting
       DT_RPATH.  Specify --disable-new-dtags to gold if you do
       not want the default DT_RUNPATH behavior. This rpath
       treatment as well as static/dynamic selection means that
       gold does not currently have ideal default behavior when
       we are using jemalloc. Thus for simplicity we don't use
       it when jemalloc is requested. An alternative to
       disabling would be to figure out all the settings
       required to make gold play nicely with jemalloc.
    #]=========================================================]
  if (("${LD_VERSION}" MATCHES "GNU gold") AND (NOT jemalloc))
    target_link_libraries (common
      INTERFACE
        -fuse-ld=gold
        -Wl,--no-as-needed
        #[=========================================================[
           see https://bugs.launchpad.net/ubuntu/+source/eglibc/+bug/1253638/comments/5
           DT_RUNPATH does not work great for transitive
           dependencies (of which boost has a few) - so just
           switch to DT_RPATH if doing dynamic linking with gold
        #]=========================================================]
        $<$<NOT:$<BOOL:${static}>>:-Wl,--disable-new-dtags>)
  endif ()
  unset (LD_VERSION)
endif ()

if (use_lld)
  # use lld linker if available
  execute_process (
    COMMAND ${CMAKE_CXX_COMPILER} -fuse-ld=lld -Wl,--version
    ERROR_QUIET OUTPUT_VARIABLE LD_VERSION)
  if ("${LD_VERSION}" MATCHES "LLD")
    target_link_libraries (common INTERFACE -fuse-ld=lld)
  endif ()
  unset (LD_VERSION)
endif()

if (assert)
  foreach (var_ CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELEASE)
    STRING (REGEX REPLACE "[-/]DNDEBUG" "" ${var_} "${${var_}}")
  endforeach ()
endif ()

#[===================================================================[
   rippled compile options/settings via an interface library
#]===================================================================]

add_library (opts INTERFACE)
add_library (Ripple::opts ALIAS opts)
target_compile_definitions (opts
  INTERFACE
    BOOST_ASIO_DISABLE_HANDLER_TYPE_REQUIREMENTS
    $<$<BOOL:${boost_show_deprecated}>:
      BOOST_ASIO_NO_DEPRECATED
      BOOST_FILESYSTEM_NO_DEPRECATED
    >
    $<$<NOT:$<BOOL:${boost_show_deprecated}>>:
      BOOST_COROUTINES_NO_DEPRECATION_WARNING
      BOOST_BEAST_ALLOW_DEPRECATED
      BOOST_FILESYSTEM_DEPRECATED
    >
    $<$<BOOL:${beast_hashers}>:
      USE_BEAST_HASHER
    >
    $<$<BOOL:${beast_no_unit_test_inline}>:BEAST_NO_UNIT_TEST_INLINE=1>
    $<$<BOOL:${beast_disable_autolink}>:BEAST_DONT_AUTOLINK_TO_WIN32_LIBRARIES=1>
    $<$<BOOL:${single_io_service_thread}>:RIPPLE_SINGLE_IO_SERVICE_THREAD=1>
    # doesn't currently compile ? :
    $<$<BOOL:${verify_nodeobject_keys}>:RIPPLE_VERIFY_NODEOBJECT_KEYS=1>)
target_compile_options (opts
  INTERFACE
    $<$<AND:$<BOOL:${is_gcc}>,$<COMPILE_LANGUAGE:CXX>>:-Wsuggest-override>
    $<$<BOOL:${perf}>:-fno-omit-frame-pointer>
    $<$<AND:$<BOOL:${is_gcc}>,$<BOOL:${coverage}>>:-fprofile-arcs -ftest-coverage>
    $<$<AND:$<BOOL:${is_clang}>,$<BOOL:${coverage}>>:-fprofile-instr-generate -fcoverage-mapping>
    $<$<BOOL:${profile}>:-pg>
    $<$<AND:$<BOOL:${is_gcc}>,$<BOOL:${profile}>>:-p>)

target_link_libraries (opts
  INTERFACE
    $<$<AND:$<BOOL:${is_gcc}>,$<BOOL:${coverage}>>:-fprofile-arcs -ftest-coverage>
    $<$<AND:$<BOOL:${is_clang}>,$<BOOL:${coverage}>>:-fprofile-instr-generate -fcoverage-mapping>
    $<$<BOOL:${profile}>:-pg>
    $<$<AND:$<BOOL:${is_gcc}>,$<BOOL:${profile}>>:-p>)

if (jemalloc)
  if (static)
    set(JEMALLOC_USE_STATIC ON CACHE BOOL "" FORCE)
  endif ()
  find_package (jemalloc REQUIRED)
  target_compile_definitions (opts INTERFACE PROFILE_JEMALLOC)
  target_include_directories (opts SYSTEM INTERFACE ${JEMALLOC_INCLUDE_DIRS})
  target_link_libraries (opts INTERFACE ${JEMALLOC_LIBRARIES})
  get_filename_component (JEMALLOC_LIB_PATH ${JEMALLOC_LIBRARIES} DIRECTORY)
  ## TODO see if we can use the BUILD_RPATH target property (is it transitive?)
  set (CMAKE_BUILD_RPATH ${CMAKE_BUILD_RPATH} ${JEMALLOC_LIB_PATH})
endif ()

if (san)
  target_compile_options (opts
    INTERFACE
      # sanitizers recommend minimum of -O1 for reasonable performance
      $<$<CONFIG:Debug>:-O1>
      ${SAN_FLAG}
      -fno-omit-frame-pointer)
  target_compile_definitions (opts
    INTERFACE
      $<$<STREQUAL:${san},address>:SANITIZER=ASAN>
      $<$<STREQUAL:${san},thread>:SANITIZER=TSAN>
      $<$<STREQUAL:${san},memory>:SANITIZER=MSAN>
      $<$<STREQUAL:${san},undefined>:SANITIZER=UBSAN>)
  target_link_libraries (opts INTERFACE ${SAN_FLAG} ${SAN_LIB})
endif ()

#[===================================================================[
   rippled transitive library deps via an interface library
#]===================================================================]

add_library (ripple_syslibs INTERFACE)
add_library (Ripple::syslibs ALIAS ripple_syslibs)
target_link_libraries (ripple_syslibs
  INTERFACE
    $<$<BOOL:${MSVC}>:
      legacy_stdio_definitions.lib
      Shlwapi
      kernel32
      user32
      gdi32
      winspool
      comdlg32
      advapi32
      shell32
      ole32
      oleaut32
      uuid
      odbc32
      odbccp32
      crypt32
    >
    $<$<NOT:$<BOOL:${MSVC}>>:dl>
    $<$<NOT:$<OR:$<BOOL:${MSVC}>,$<BOOL:${APPLE}>>>:rt>)

if (NOT MSVC)
  set (THREADS_PREFER_PTHREAD_FLAG ON)
  find_package (Threads)
  target_link_libraries (ripple_syslibs INTERFACE Threads::Threads)
endif ()

add_library (ripple_libs INTERFACE)
add_library (Ripple::libs ALIAS ripple_libs)
target_link_libraries (ripple_libs INTERFACE Ripple::syslibs)

#[===================================================================[
   NIH dep: boost
#]===================================================================]

if ((NOT DEFINED BOOST_ROOT) AND (DEFINED ENV{BOOST_ROOT}))
  set (BOOST_ROOT $ENV{BOOST_ROOT})
endif ()
file (TO_CMAKE_PATH "${BOOST_ROOT}" BOOST_ROOT)
if (WIN32 OR CYGWIN)
  # Workaround for MSVC having two boost versions - x86 and x64 on same PC in stage folders
  if (DEFINED BOOST_ROOT)
    if (IS_DIRECTORY ${BOOST_ROOT}/stage64/lib)
      set (BOOST_LIBRARYDIR ${BOOST_ROOT}/stage64/lib)
    else ()
      set (BOOST_LIBRARYDIR ${BOOST_ROOT}/stage/lib)
    endif ()
  endif ()
endif ()
message (STATUS "BOOST_ROOT: ${BOOST_ROOT}")
message (STATUS "BOOST_LIBRARYDIR: ${BOOST_LIBRARYDIR}")

# uncomment the following as needed to debug FindBoost issues:
#set (Boost_DEBUG ON)

#[=========================================================[
   boost dynamic libraries don't trivially support @rpath
   linking right now (cmake's default), so just force
   static linking for macos, or if requested on linux by flag
#]=========================================================]
if (static)
  set (Boost_USE_STATIC_LIBS ON)
endif ()
set (Boost_USE_MULTITHREADED ON)
if (static AND NOT APPLE)
  set (Boost_USE_STATIC_RUNTIME ON)
else ()
  set (Boost_USE_STATIC_RUNTIME OFF)
endif ()
find_package (Boost 1.70 REQUIRED
  COMPONENTS
    chrono
    context
    coroutine
    date_time
    filesystem
    program_options
    regex
    serialization
    system
    thread)

add_library (ripple_boost INTERFACE)
add_library (Ripple::boost ALIAS ripple_boost)
if (is_xcode)
  target_include_directories (ripple_boost BEFORE INTERFACE ${Boost_INCLUDE_DIRS})
  target_compile_options (ripple_boost INTERFACE --system-header-prefix="boost/")
else ()
  target_include_directories (ripple_boost SYSTEM BEFORE INTERFACE ${Boost_INCLUDE_DIRS})
endif()

target_link_libraries (ripple_boost
  INTERFACE
    Boost::boost
    Boost::chrono
    Boost::coroutine
    Boost::date_time
    Boost::filesystem
    Boost::program_options
    Boost::regex
    Boost::serialization
    Boost::system
    Boost::thread)
if (san)
  if (NOT Boost_INCLUDE_DIRS AND TARGET Boost::headers)
    get_target_property (Boost_INCLUDE_DIRS Boost::headers INTERFACE_INCLUDE_DIRECTORIES)
  endif ()
  message(STATUS "Adding [${Boost_INCLUDE_DIRS}] to sanitizer blacklist")
  file (WRITE ${CMAKE_CURRENT_BINARY_DIR}/san_bl.txt "src:${Boost_INCLUDE_DIRS}/*")
  target_compile_options (opts
    INTERFACE
      # ignore boost headers for sanitizing
      -fsanitize-blacklist=${CMAKE_CURRENT_BINARY_DIR}/san_bl.txt)
endif ()

# workaround for xcode 10.2 and boost < 1.69
# once we require Boost 1.69 or higher, this can be removed
# see:  https://github.com/boostorg/asio/commit/43874d5
if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND
    CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 10.0.1.10010043 AND
    Boost_VERSION LESS 106900)
  target_compile_definitions (opts INTERFACE BOOST_ASIO_HAS_STD_STRING_VIEW)
endif ()

#[===================================================================[
   NIH dep: openssl
#]===================================================================]

#[===============================================[
  OPENSSL_ROOT_DIR is the only variable that
  FindOpenSSL honors for locating, so convert any
  OPENSSL_ROOT vars to this
#]===============================================]
if (NOT DEFINED OPENSSL_ROOT_DIR)
  if (DEFINED ENV{OPENSSL_ROOT})
    set (OPENSSL_ROOT_DIR $ENV{OPENSSL_ROOT})
  elseif (HOMEBREW)
    execute_process (COMMAND ${HOMEBREW} --prefix openssl
      OUTPUT_VARIABLE OPENSSL_ROOT_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif ()
  file (TO_CMAKE_PATH "${OPENSSL_ROOT_DIR}" OPENSSL_ROOT_DIR)
endif ()

if (static)
  set (OPENSSL_USE_STATIC_LIBS ON)
endif ()
set (OPENSSL_MSVC_STATIC_RT ON)
find_package (OpenSSL 1.0.2 REQUIRED)
target_link_libraries (ripple_libs
  INTERFACE
    OpenSSL::SSL
    OpenSSL::Crypto)
# disable SSLv2...this can also be done when building/configuring OpenSSL
set_target_properties(OpenSSL::SSL PROPERTIES
    INTERFACE_COMPILE_DEFINITIONS OPENSSL_NO_SSL2)
#[=========================================================[
   https://gitlab.kitware.com/cmake/cmake/issues/16885
   depending on how openssl is built, it might depend
   on zlib. In fact, the openssl find package should
   figure this out for us, but it does not currently...
   so let's add zlib ourselves to the lib list
   TODO: investigate linking to static zlib for static
   build option
#]=========================================================]
find_package (ZLIB)
set (has_zlib FALSE)
if (TARGET ZLIB::ZLIB)
  set_target_properties(OpenSSL::Crypto PROPERTIES
    INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)
  set (has_zlib TRUE)
endif ()

#[===================================================================[
   NIH dep: secp256k1
#]===================================================================]

add_library (secp256k1 STATIC
  src/secp256k1/src/secp256k1.c)
target_compile_definitions (secp256k1
  PRIVATE
    USE_NUM_NONE
    USE_FIELD_10X26
    USE_FIELD_INV_BUILTIN
    USE_SCALAR_8X32
    USE_SCALAR_INV_BUILTIN)
target_include_directories (secp256k1
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/secp256k1)
target_compile_options (secp256k1
  PRIVATE
    $<$<BOOL:${MSVC}>:-wd4319>
    $<$<NOT:$<BOOL:${MSVC}>>:
      -Wno-deprecated-declarations
      -Wno-unused-function
    >
    $<$<BOOL:${is_gcc}>:-Wno-nonnull-compare>)
add_library (NIH::secp256k1 ALIAS secp256k1)
target_link_libraries (ripple_libs INTERFACE NIH::secp256k1)
#[===========================[
   headers installation
#]===========================]
install (
  FILES
    src/secp256k1/include/secp256k1.h
  DESTINATION include/secp256k1/include)

#[===================================================================[
   NIH dep: ed25519-donna
#]===================================================================]

add_library (ed25519-donna STATIC
  src/ed25519-donna/ed25519.c)
target_include_directories (ed25519-donna
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ed25519-donna)
#[=========================================================[
   NOTE for macos:
   https://github.com/floodyberry/ed25519-donna/issues/29
   our source for ed25519-donna-portable.h has been
   patched to workaround this.
#]=========================================================]
target_link_libraries (ed25519-donna PUBLIC OpenSSL::SSL)
add_library (NIH::ed25519-donna ALIAS ed25519-donna)
target_link_libraries (ripple_libs INTERFACE NIH::ed25519-donna)
#[===========================[
   headers installation
#]===========================]
install (
  FILES
    src/ed25519-donna/ed25519.h
  DESTINATION include/ed25519-donna)

#[===================================================================[
   NIH dep: lz4
#]===================================================================]

ExternalProject_Add (lz4
  PREFIX ${nih_cache_path}
  GIT_REPOSITORY https://github.com/lz4/lz4.git
  GIT_TAG v1.8.2
  SOURCE_SUBDIR contrib/cmake_unofficial
  CMAKE_ARGS
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
    -DCMAKE_DEBUG_POSTFIX=_d
    $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
    -DBUILD_STATIC_LIBS=ON
    -DBUILD_SHARED_LIBS=OFF
    $<$<BOOL:${MSVC}>:
      "-DCMAKE_C_FLAGS=-GR -Gd -fp:precise -FS -MP"
      "-DCMAKE_C_FLAGS_DEBUG=-MTd"
      "-DCMAKE_C_FLAGS_RELEASE=-MT"
    >
  LOG_BUILD ON
  LOG_CONFIGURE ON
  BUILD_COMMAND
    ${CMAKE_COMMAND}
    --build .
    --config $<CONFIG>
    --target lz4_static
    $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel ${ep_procs}>
    $<$<BOOL:${is_multiconfig}>:
      COMMAND
        ${CMAKE_COMMAND} -E copy
        <BINARY_DIR>/$<CONFIG>/${ep_lib_prefix}lz4$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
        <BINARY_DIR>
      >
  TEST_COMMAND ""
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS
    <BINARY_DIR>/${ep_lib_prefix}lz4${ep_lib_suffix}
    <BINARY_DIR>/${ep_lib_prefix}lz4_d${ep_lib_suffix}
)
ExternalProject_Get_Property (lz4 BINARY_DIR)
ExternalProject_Get_Property (lz4 SOURCE_DIR)
if (CMAKE_VERBOSE_MAKEFILE)
  print_ep_logs (lz4)
endif ()
add_library (lz4_lib STATIC IMPORTED GLOBAL)
file (MAKE_DIRECTORY ${SOURCE_DIR}/lz4)
set_target_properties (lz4_lib PROPERTIES
  IMPORTED_LOCATION_DEBUG
    ${BINARY_DIR}/${ep_lib_prefix}lz4_d${ep_lib_suffix}
  IMPORTED_LOCATION_RELEASE
    ${BINARY_DIR}/${ep_lib_prefix}lz4${ep_lib_suffix}
  INTERFACE_INCLUDE_DIRECTORIES
    ${SOURCE_DIR}/lib)
add_dependencies (lz4_lib lz4)
target_link_libraries (ripple_libs INTERFACE lz4_lib)
exclude_if_included (lz4)
exclude_if_included (lz4_lib)

#[===================================================================[
   NIH dep: libarchive
#]===================================================================]

set (lib_post "")
if (MSVC)
  set (lib_post "_static")
endif ()
ExternalProject_Add (libarchive
  PREFIX ${nih_cache_path}
  GIT_REPOSITORY https://github.com/libarchive/libarchive.git
  GIT_TAG v3.3.3
  CMAKE_ARGS
    # passing the compiler seems to be needed for windows CI, sadly
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
    -DCMAKE_DEBUG_POSTFIX=_d
    $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
    -DENABLE_LZ4=ON
    -ULZ4_*
    -DLZ4_INCLUDE_DIR=$<JOIN:$<TARGET_PROPERTY:lz4_lib,INTERFACE_INCLUDE_DIRECTORIES>,::>
    # because we are building a static lib, this lz4 library doesn't
    # actually matter since you can't generally link static libs to other static
    # libs. The include files are needed, but the library itself is not (until
    # we link our application, at which point we use the lz4 we built above).
    # nonetheless, we need to provide a library to libarchive else it will
    # NOT include lz4 support when configuring
    -DLZ4_LIBRARY=$<IF:$<CONFIG:Debug>,$<TARGET_PROPERTY:lz4_lib,IMPORTED_LOCATION_DEBUG>,$<TARGET_PROPERTY:lz4_lib,IMPORTED_LOCATION_RELEASE>>
    -DENABLE_WERROR=OFF
    -DENABLE_TAR=OFF
    -DENABLE_TAR_SHARED=OFF
    -DENABLE_INSTALL=ON
    -DENABLE_NETTLE=OFF
    -DENABLE_OPENSSL=OFF
    -DENABLE_LZO=OFF
    -DENABLE_LZMA=OFF
    -DENABLE_ZLIB=OFF
    -DENABLE_BZip2=OFF
    -DENABLE_LIBXML2=OFF
    -DENABLE_EXPAT=OFF
    -DENABLE_PCREPOSIX=OFF
    -DENABLE_LibGCC=OFF
    -DENABLE_CNG=OFF
    -DENABLE_CPIO=OFF
    -DENABLE_CPIO_SHARED=OFF
    -DENABLE_CAT=OFF
    -DENABLE_CAT_SHARED=OFF
    -DENABLE_XATTR=OFF
    -DENABLE_ACL=OFF
    -DENABLE_ICONV=OFF
    -DENABLE_TEST=OFF
    -DENABLE_COVERAGE=OFF
    $<$<BOOL:${MSVC}>:
      "-DCMAKE_C_FLAGS=-GR -Gd -fp:precise -FS -MP"
      "-DCMAKE_C_FLAGS_DEBUG=-MTd"
      "-DCMAKE_C_FLAGS_RELEASE=-MT"
    >
  LIST_SEPARATOR ::
  LOG_BUILD ON
  LOG_CONFIGURE ON
  BUILD_COMMAND
    ${CMAKE_COMMAND}
    --build .
    --config $<CONFIG>
    --target archive_static
    $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel ${ep_procs}>
    $<$<BOOL:${is_multiconfig}>:
      COMMAND
        ${CMAKE_COMMAND} -E copy
          <BINARY_DIR>/libarchive/$<CONFIG>/${ep_lib_prefix}archive${lib_post}$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
          <BINARY_DIR>/libarchive
      >
  TEST_COMMAND ""
  INSTALL_COMMAND ""
  DEPENDS lz4
  BUILD_BYPRODUCTS
    <BINARY_DIR>/libarchive/${ep_lib_prefix}archive${lib_post}${ep_lib_suffix}
    <BINARY_DIR>/libarchive/${ep_lib_prefix}archive${lib_post}_d${ep_lib_suffix}
)
ExternalProject_Get_Property (libarchive BINARY_DIR)
ExternalProject_Get_Property (libarchive SOURCE_DIR)
if (CMAKE_VERBOSE_MAKEFILE)
  print_ep_logs (libarchive)
endif ()
add_library (archive_lib STATIC IMPORTED GLOBAL)
file (MAKE_DIRECTORY ${SOURCE_DIR}/libarchive)
set_target_properties (archive_lib PROPERTIES
  IMPORTED_LOCATION_DEBUG
    ${BINARY_DIR}/libarchive/${ep_lib_prefix}archive${lib_post}_d${ep_lib_suffix}
  IMPORTED_LOCATION_RELEASE
    ${BINARY_DIR}/libarchive/${ep_lib_prefix}archive${lib_post}${ep_lib_suffix}
  INTERFACE_INCLUDE_DIRECTORIES
    ${SOURCE_DIR}/libarchive
  INTERFACE_COMPILE_DEFINITIONS
    LIBARCHIVE_STATIC)
add_dependencies (archive_lib libarchive)
target_link_libraries (archive_lib INTERFACE lz4_lib)
target_link_libraries (ripple_libs INTERFACE archive_lib)
exclude_if_included (libarchive)
exclude_if_included (archive_lib)

#[===================================================================[
   NIH dep: sqlite
#]===================================================================]

ExternalProject_Add (sqlite3
  PREFIX ${nih_cache_path}
  # sqlite doesn't use git, but it provides versioned tarballs
  URL https://www.sqlite.org/2018/sqlite-amalgamation-3260000.zip
  # ^^^ version is apparent in the URL:  3260000 => 3.26.0
  URL_HASH SHA256=de5dcab133aa339a4cf9e97c40aa6062570086d6085d8f9ad7bc6ddf8a52096e
  # we wrote a very simple CMake file to build sqlite
  # so that's what we copy here so that we can build with
  # CMake. sqlite doesn't generally provided a build system
  # for the single amalgamation source file.
  PATCH_COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/Builds/CMake/CMake_sqlite3.txt
    <SOURCE_DIR>/CMakeLists.txt
  CMAKE_ARGS
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
    -DCMAKE_DEBUG_POSTFIX=_d
    $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
    $<$<BOOL:${MSVC}>:
      "-DCMAKE_C_FLAGS=-GR -Gd -fp:precise -FS -MP"
      "-DCMAKE_C_FLAGS_DEBUG=-MTd"
      "-DCMAKE_C_FLAGS_RELEASE=-MT"
    >
  LOG_BUILD ON
  LOG_CONFIGURE ON
  BUILD_COMMAND
    ${CMAKE_COMMAND}
    --build .
    --config $<CONFIG>
    $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel ${ep_procs}>
    $<$<BOOL:${is_multiconfig}>:
      COMMAND
        ${CMAKE_COMMAND} -E copy
          <BINARY_DIR>/$<CONFIG>/${ep_lib_prefix}sqlite3$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
          <BINARY_DIR>
      >
  TEST_COMMAND ""
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS
    <BINARY_DIR>/${ep_lib_prefix}sqlite3${ep_lib_suffix}
    <BINARY_DIR>/${ep_lib_prefix}sqlite3_d${ep_lib_suffix}
)
ExternalProject_Get_Property (sqlite3 BINARY_DIR)
ExternalProject_Get_Property (sqlite3 SOURCE_DIR)
if (CMAKE_VERBOSE_MAKEFILE)
  print_ep_logs (sqlite3)
endif ()
add_library (sqlite STATIC IMPORTED GLOBAL)
set_target_properties (sqlite PROPERTIES
  IMPORTED_LOCATION_DEBUG
    ${BINARY_DIR}/${ep_lib_prefix}sqlite3_d${ep_lib_suffix}
  IMPORTED_LOCATION_RELEASE
    ${BINARY_DIR}/${ep_lib_prefix}sqlite3${ep_lib_suffix}
  INTERFACE_INCLUDE_DIRECTORIES
    ${SOURCE_DIR})
add_dependencies (sqlite sqlite3)
target_link_libraries (sqlite INTERFACE $<$<NOT:$<BOOL:${MSVC}>>:dl>)
target_link_libraries (ripple_libs INTERFACE sqlite)
exclude_if_included (sqlite3)
exclude_if_included (sqlite)
set(sqlite_BINARY_DIR ${BINARY_DIR})

#[===================================================================[
   NIH dep: soci
#]===================================================================]

set (soci_lib_pre ${ep_lib_prefix})
set (soci_lib_post "")
if (WIN32)
  # for some reason soci on windows still prepends lib (non-standard)
  set (soci_lib_pre lib)
  # this version in the name might change if/when we change versions of soci
  set (soci_lib_post "_4_0")
endif ()
get_target_property (_boost_incs Boost::date_time INTERFACE_INCLUDE_DIRECTORIES)
ExternalProject_Add (soci
  PREFIX ${nih_cache_path}
  GIT_REPOSITORY https://github.com/SOCI/soci.git
  GIT_TAG 04e1870294918d20761736743bb6136314c42dd5
  # We had an issue with soci integer range checking for boost::optional
  # and needed to remove the exception that SOCI throws in this case.
  # This is *probably* a bug in SOCI, but has never been investigated more
  # nor reported to the maintainers.
  # This cmake script comments out the lines in question.
  # This patch process is likely fragile and should be reviewed carefully
  # whenever we update the GIT_TAG above.
  PATCH_COMMAND
    ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/Builds/CMake/soci_patch.cmake
  CMAKE_ARGS
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
    -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/sqlite3
    -DCMAKE_MODULE_PATH=${CMAKE_CURRENT_SOURCE_DIR}/Builds/CMake
    -DCMAKE_INCLUDE_PATH=$<JOIN:$<TARGET_PROPERTY:sqlite,INTERFACE_INCLUDE_DIRECTORIES>,::>
    -DCMAKE_LIBRARY_PATH=${sqlite_BINARY_DIR}
    -DCMAKE_DEBUG_POSTFIX=_d
    $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
    -DSOCI_CXX_C11=ON
    -DSOCI_STATIC=ON
    -DSOCI_LIBDIR=lib
    -DSOCI_SHARED=OFF
    -DSOCI_TESTS=OFF
    # hack to workaround the fact that soci doesn't currently use
    # boost imported targets in its cmake. If they switch to
    # proper imported targets, this next line can be removed
    # (as well as the get_property above that sets _boost_incs)
    -DBoost_INCLUDE_DIRS=$<JOIN:${_boost_incs},::>
    -DBOOST_ROOT=${BOOST_ROOT}
    -DWITH_BOOST=ON
    -DSOCI_DB2=OFF
    -DSOCI_FIREBIRD=OFF
    -DSOCI_MYSQL=OFF
    -DSOCI_ODBC=OFF
    -DSOCI_ORACLE=OFF
    -DSOCI_POSTGRESQL=OFF
    -DSOCI_SQLITE3=ON
    -DSQLITE3_INCLUDE_DIR=$<JOIN:$<TARGET_PROPERTY:sqlite,INTERFACE_INCLUDE_DIRECTORIES>,::>
    -DSQLITE3_LIBRARY=$<IF:$<CONFIG:Debug>,$<TARGET_PROPERTY:sqlite,IMPORTED_LOCATION_DEBUG>,$<TARGET_PROPERTY:sqlite,IMPORTED_LOCATION_RELEASE>>
    $<$<BOOL:${APPLE}>:-DCMAKE_FIND_FRAMEWORK=LAST>
    $<$<BOOL:${MSVC}>:
      "-DCMAKE_CXX_FLAGS=-GR -Gd -fp:precise -FS -EHa -MP"
      "-DCMAKE_CXX_FLAGS_DEBUG=-MTd"
      "-DCMAKE_CXX_FLAGS_RELEASE=-MT"
    >
    $<$<NOT:$<BOOL:${MSVC}>>:
      "-DCMAKE_CXX_FLAGS=-Wno-deprecated-declarations"
    >
    # SEE: https://github.com/SOCI/soci/issues/640
    $<$<AND:$<BOOL:${is_gcc}>,$<VERSION_GREATER_EQUAL:${CMAKE_CXX_COMPILER_VERSION},8>>:
      "-DCMAKE_CXX_FLAGS=-Wno-deprecated-declarations -Wno-error=format-overflow -Wno-format-overflow -Wno-error=format-truncation"
    >
  LIST_SEPARATOR ::
  LOG_BUILD ON
  LOG_CONFIGURE ON
  BUILD_COMMAND
    ${CMAKE_COMMAND}
    --build .
    --config $<CONFIG>
    $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel ${ep_procs}>
    $<$<BOOL:${is_multiconfig}>:
      COMMAND
        ${CMAKE_COMMAND} -E copy
        <BINARY_DIR>/lib/$<CONFIG>/${soci_lib_pre}soci_core${soci_lib_post}$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
        <BINARY_DIR>/lib/$<CONFIG>/${soci_lib_pre}soci_empty${soci_lib_post}$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
        <BINARY_DIR>/lib/$<CONFIG>/${soci_lib_pre}soci_sqlite3${soci_lib_post}$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
        <BINARY_DIR>/lib
      >
  TEST_COMMAND ""
  INSTALL_COMMAND ""
  DEPENDS sqlite3
  BUILD_BYPRODUCTS
    <BINARY_DIR>/lib/${soci_lib_pre}soci_core${soci_lib_post}${ep_lib_suffix}
    <BINARY_DIR>/lib/${soci_lib_pre}soci_core${soci_lib_post}_d${ep_lib_suffix}
    <BINARY_DIR>/lib/${soci_lib_pre}soci_empty${soci_lib_post}${ep_lib_suffix}
    <BINARY_DIR>/lib/${soci_lib_pre}soci_empty${soci_lib_post}_d${ep_lib_suffix}
    <BINARY_DIR>/lib/${soci_lib_pre}soci_sqlite3${soci_lib_post}${ep_lib_suffix}
    <BINARY_DIR>/lib/${soci_lib_pre}soci_sqlite3${soci_lib_post}_d${ep_lib_suffix}
)
ExternalProject_Get_Property (soci BINARY_DIR)
ExternalProject_Get_Property (soci SOURCE_DIR)
if (CMAKE_VERBOSE_MAKEFILE)
  print_ep_logs (soci)
endif ()
file (MAKE_DIRECTORY ${SOURCE_DIR}/include)
file (MAKE_DIRECTORY ${BINARY_DIR}/include)
foreach (_comp core empty sqlite3)
  add_library ("soci_${_comp}" STATIC IMPORTED GLOBAL)
  set_target_properties ("soci_${_comp}" PROPERTIES
    IMPORTED_LOCATION_DEBUG
      ${BINARY_DIR}/lib/${soci_lib_pre}soci_${_comp}${soci_lib_post}_d${ep_lib_suffix}
    IMPORTED_LOCATION_RELEASE
      ${BINARY_DIR}/lib/${soci_lib_pre}soci_${_comp}${soci_lib_post}${ep_lib_suffix}
    INTERFACE_INCLUDE_DIRECTORIES
      "${SOURCE_DIR}/include;${BINARY_DIR}/include")
  add_dependencies ("soci_${_comp}" soci) # something has to depend on the ExternalProject to trigger it
  target_link_libraries (ripple_libs INTERFACE "soci_${_comp}")
  if (NOT _comp STREQUAL "core")
    target_link_libraries ("soci_${_comp}" INTERFACE soci_core)
  endif ()
  exclude_if_included ("soci_${_comp}")
endforeach ()
exclude_if_included (soci)

#[===================================================================[
   NIH dep: snappy
#]===================================================================]

ExternalProject_Add (snappy
  PREFIX ${nih_cache_path}
  GIT_REPOSITORY https://github.com/google/snappy.git
  GIT_TAG 1.1.7
  CMAKE_ARGS
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
    -DCMAKE_DEBUG_POSTFIX=_d
    $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
    -DBUILD_SHARED_LIBS=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DSNAPPY_BUILD_TESTS=OFF
    $<$<BOOL:${MSVC}>:
      "-DCMAKE_CXX_FLAGS=-GR -Gd -fp:precise -FS -EHa -MP"
      "-DCMAKE_CXX_FLAGS_DEBUG=-MTd"
      "-DCMAKE_CXX_FLAGS_RELEASE=-MT"
    >
  LOG_BUILD ON
  LOG_CONFIGURE ON
  BUILD_COMMAND
    ${CMAKE_COMMAND}
    --build .
    --config $<CONFIG>
    $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel ${ep_procs}>
    $<$<BOOL:${is_multiconfig}>:
      COMMAND
        ${CMAKE_COMMAND} -E copy
        <BINARY_DIR>/$<CONFIG>/${ep_lib_prefix}snappy$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
        <BINARY_DIR>
      >
  TEST_COMMAND ""
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS
    <BINARY_DIR>/${ep_lib_prefix}snappy${ep_lib_suffix}
    <BINARY_DIR>/${ep_lib_prefix}snappy_d${ep_lib_suffix}
)
ExternalProject_Get_Property (snappy BINARY_DIR)
ExternalProject_Get_Property (snappy SOURCE_DIR)
if (CMAKE_VERBOSE_MAKEFILE)
  print_ep_logs (snappy)
endif ()
add_library (snappy_lib STATIC IMPORTED GLOBAL)
file (MAKE_DIRECTORY ${SOURCE_DIR}/snappy)
set_target_properties (snappy_lib PROPERTIES
  IMPORTED_LOCATION_DEBUG
    ${BINARY_DIR}/${ep_lib_prefix}snappy_d${ep_lib_suffix}
  IMPORTED_LOCATION_RELEASE
    ${BINARY_DIR}/${ep_lib_prefix}snappy${ep_lib_suffix}
  INTERFACE_INCLUDE_DIRECTORIES
    "${SOURCE_DIR};${BINARY_DIR}")
add_dependencies (snappy_lib snappy)
target_link_libraries (ripple_libs INTERFACE snappy_lib)
exclude_if_included (snappy)
exclude_if_included (snappy_lib)

#[===================================================================[
   NIH dep: rocksdb
#]===================================================================]

ExternalProject_Add (rocksdb
  PREFIX ${nih_cache_path}
  GIT_REPOSITORY https://github.com/facebook/rocksdb.git
  GIT_TAG v5.17.2
  PATCH_COMMAND
    # only used by windows build
    ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/Builds/CMake/rocks_thirdparty.inc
    <SOURCE_DIR>/thirdparty.inc
  COMMAND
    # fixup their build version file to keep the values
    # from changing always
    ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/Builds/CMake/rocksdb_build_version.cc.in
    <SOURCE_DIR>/util/build_version.cc.in
  CMAKE_ARGS
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
    -DCMAKE_DEBUG_POSTFIX=_d
    $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
    -DBUILD_SHARED_LIBS=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DWITH_JEMALLOC=$<IF:$<BOOL:${jemalloc}>,ON,OFF>
    -DWITH_SNAPPY=ON
    -DWITH_LZ4=ON
    -DWITH_ZLIB=OFF
    -DUSE_RTTI=ON
    -DWITH_ZSTD=OFF
    -DWITH_GFLAGS=OFF
    -DWITH_BZ2=OFF
    -ULZ4_*
    -DLZ4_INCLUDE_DIR=$<JOIN:$<TARGET_PROPERTY:lz4_lib,INTERFACE_INCLUDE_DIRECTORIES>,::>
    -DLZ4_LIBRARIES=$<IF:$<CONFIG:Debug>,$<TARGET_PROPERTY:lz4_lib,IMPORTED_LOCATION_DEBUG>,$<TARGET_PROPERTY:lz4_lib,IMPORTED_LOCATION_RELEASE>>
    -DLZ4_FOUND=ON
    -USNAPPY_*
    -DSNAPPY_INCLUDE_DIR=$<JOIN:$<TARGET_PROPERTY:snappy_lib,INTERFACE_INCLUDE_DIRECTORIES>,::>
    -DSNAPPY_LIBRARIES=$<IF:$<CONFIG:Debug>,$<TARGET_PROPERTY:snappy_lib,IMPORTED_LOCATION_DEBUG>,$<TARGET_PROPERTY:snappy_lib,IMPORTED_LOCATION_RELEASE>>
    -DSNAPPY_FOUND=ON
    -DWITH_MD_LIBRARY=OFF
    -DWITH_RUNTIME_DEBUG=$<IF:$<CONFIG:Debug>,ON,OFF>
    -DFAIL_ON_WARNINGS=OFF
    -DWITH_ASAN=OFF
    -DWITH_TSAN=OFF
    -DWITH_UBSAN=OFF
    -DWITH_NUMA=OFF
    -DWITH_TBB=OFF
    -DWITH_WINDOWS_UTF8_FILENAMES=OFF
    -DWITH_XPRESS=OFF
    -DPORTABLE=ON
    -DFORCE_SSE42=OFF
    -DDISABLE_STALL_NOTIF=OFF
    -DOPTDBG=ON
    -DROCKSDB_LITE=OFF
    -DWITH_FALLOCATE=ON
    -DWITH_LIBRADOS=OFF
    -DWITH_JNI=OFF
    -DROCKSDB_INSTALL_ON_WINDOWS=OFF
    -DWITH_TESTS=OFF
    -DWITH_TOOLS=OFF
    $<$<BOOL:${MSVC}>:
      "-DCMAKE_CXX_FLAGS=-GR -Gd -fp:precise -FS -MP /DNDEBUG"
    >
    $<$<NOT:$<BOOL:${MSVC}>>:
      "-DCMAKE_CXX_FLAGS=-DNDEBUG"
    >
  LOG_BUILD ON
  LOG_CONFIGURE ON
  BUILD_COMMAND
    ${CMAKE_COMMAND}
    --build .
    --config $<CONFIG>
    $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel ${ep_procs}>
    $<$<BOOL:${is_multiconfig}>:
      COMMAND
        ${CMAKE_COMMAND} -E copy
        <BINARY_DIR>/$<CONFIG>/${ep_lib_prefix}rocksdb$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
        <BINARY_DIR>
      >
  LIST_SEPARATOR ::
  TEST_COMMAND ""
  INSTALL_COMMAND ""
  DEPENDS snappy lz4
  BUILD_BYPRODUCTS
    <BINARY_DIR>/${ep_lib_prefix}rocksdb${ep_lib_suffix}
    <BINARY_DIR>/${ep_lib_prefix}rocksdb_d${ep_lib_suffix}
)
ExternalProject_Get_Property (rocksdb BINARY_DIR)
ExternalProject_Get_Property (rocksdb SOURCE_DIR)
if (CMAKE_VERBOSE_MAKEFILE)
  print_ep_logs (rocksdb)
endif ()
add_library (rocksdb_lib STATIC IMPORTED GLOBAL)
file (MAKE_DIRECTORY ${SOURCE_DIR}/include)
set_target_properties (rocksdb_lib PROPERTIES
  IMPORTED_LOCATION_DEBUG
    ${BINARY_DIR}/${ep_lib_prefix}rocksdb_d${ep_lib_suffix}
  IMPORTED_LOCATION_RELEASE
    ${BINARY_DIR}/${ep_lib_prefix}rocksdb${ep_lib_suffix}
  INTERFACE_INCLUDE_DIRECTORIES
    ${SOURCE_DIR}/include
  INTERFACE_COMPILE_DEFINITIONS
    RIPPLE_ROCKSDB_AVAILABLE=1)
add_dependencies (rocksdb_lib rocksdb)
target_link_libraries (rocksdb_lib INTERFACE snappy_lib lz4_lib)
if (MSVC)
  target_link_libraries (rocksdb_lib INTERFACE rpcrt4)
endif ()
target_link_libraries (ripple_libs INTERFACE rocksdb_lib)
exclude_if_included (rocksdb)
exclude_if_included (rocksdb_lib)

#[===================================================================[
   NIH dep: nudb

   NuDB is header-only, thus is an INTERFACE lib in CMake.
   TODO: move the library definition into NuDB repo and add
   proper targets and export/install
#]===================================================================]

if (is_root_project) # NuDB not needed in the case of xrpl_core inclusion build
  add_library (nudb INTERFACE)
  if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.11)
    FetchContent_Declare(
      nudb_src
      GIT_REPOSITORY https://github.com/CPPAlliance/NuDB.git
      GIT_TAG        2.0.1
    )
    FetchContent_GetProperties(nudb_src)
    if(NOT nudb_src_POPULATED)
      message (STATUS "Pausing to download NuDB...")
      FetchContent_Populate(nudb_src)
    endif()
  else ()
    ExternalProject_Add (nudb_src
      PREFIX ${nih_cache_path}
      GIT_REPOSITORY https://github.com/CPPAlliance/NuDB.git
      GIT_TAG 2.0.1
      CONFIGURE_COMMAND ""
      BUILD_COMMAND ""
      TEST_COMMAND ""
      INSTALL_COMMAND ""
    )
    ExternalProject_Get_Property (nudb_src SOURCE_DIR)
    set (nudb_src_SOURCE_DIR "${SOURCE_DIR}")
    file (MAKE_DIRECTORY ${nudb_src_SOURCE_DIR}/include)
    add_dependencies (nudb nudb_src)
  endif ()

  file(TO_CMAKE_PATH "${nudb_src_SOURCE_DIR}" nudb_src_SOURCE_DIR)
# specify as system includes so as to avoid warnings
  target_include_directories (nudb SYSTEM INTERFACE ${nudb_src_SOURCE_DIR}/include)
  target_link_libraries (nudb
    INTERFACE
      Boost::thread
      Boost::system)
  add_library (NIH::nudb ALIAS nudb)
  target_link_libraries (ripple_libs INTERFACE NIH::nudb)
endif ()

#[===================================================================[
   import protobuf (lib and compiler) and create a lib
   from our proto message definitions. If the system protobuf
   is not found, fallback on EP to download and build a version
   from official source.
#]===================================================================]

if (static)
  set (Protobuf_USE_STATIC_LIBS ON)
endif ()
find_package (Protobuf)
if (local_protobuf OR NOT TARGET protobuf::libprotobuf)
  message (STATUS "using local protobuf build.")
  if (WIN32)
    # protobuf prepends lib even on windows
    set (pbuf_lib_pre "lib")
  else ()
    set (pbuf_lib_pre ${ep_lib_prefix})
  endif ()
  # for the external project build of protobuf, we currently ignore the
  # static option and always build static libs here. This is consistent
  # with our other EP builds. Dynamic libs in an EP would add complexity
  # because we'd need to get them into the runtime path, and probably
  # install them.
  ExternalProject_Add (protobuf_src
    PREFIX ${nih_cache_path}
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG v3.6.1
    SOURCE_SUBDIR cmake
    CMAKE_ARGS
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      -Dprotobuf_BUILD_TESTS=OFF
      -Dprotobuf_BUILD_EXAMPLES=OFF
      -Dprotobuf_BUILD_PROTOC_BINARIES=ON
      -Dprotobuf_MSVC_STATIC_RUNTIME=ON
      -DBUILD_SHARED_LIBS=OFF
      -Dprotobuf_BUILD_SHARED_LIBS=OFF
      -DCMAKE_DEBUG_POSTFIX=_d
      -Dprotobuf_DEBUG_POSTFIX=_d
      -Dprotobuf_WITH_ZLIB=$<IF:$<BOOL:${has_zlib}>,ON,OFF>
      $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
      $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
      $<$<BOOL:${MSVC}>:
	"-DCMAKE_CXX_FLAGS=-GR -Gd -fp:precise -FS -EHa -MP"
      >
    LOG_BUILD ON
    LOG_CONFIGURE ON
    BUILD_COMMAND
      ${CMAKE_COMMAND}
      --build .
      --config $<CONFIG>
      $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel ${ep_procs}>
      $<$<BOOL:${is_multiconfig}>:
        COMMAND
          ${CMAKE_COMMAND} -E copy
          <BINARY_DIR>/$<CONFIG>/${pbuf_lib_pre}protobuf$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
          <BINARY_DIR>
        COMMAND
          ${CMAKE_COMMAND} -E copy
          <BINARY_DIR>/$<CONFIG>/protoc${CMAKE_EXECUTABLE_SUFFIX}
          <BINARY_DIR>
        >
    TEST_COMMAND ""
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS
      <BINARY_DIR>/${pbuf_lib_pre}protobuf${ep_lib_suffix}
      <BINARY_DIR>/${pbuf_lib_pre}protobuf_d${ep_lib_suffix}
      <BINARY_DIR>/protoc${CMAKE_EXECUTABLE_SUFFIX}
  )
  ExternalProject_Get_Property (protobuf_src BINARY_DIR)
  ExternalProject_Get_Property (protobuf_src SOURCE_DIR)
  if (CMAKE_VERBOSE_MAKEFILE)
    print_ep_logs (protobuf_src)
  endif ()

  if (NOT TARGET protobuf::libprotobuf)
    add_library (protobuf::libprotobuf STATIC IMPORTED GLOBAL)
  endif ()
  file (MAKE_DIRECTORY ${SOURCE_DIR}/src)
  set_target_properties (protobuf::libprotobuf PROPERTIES
    IMPORTED_LOCATION_DEBUG
      ${BINARY_DIR}/${pbuf_lib_pre}protobuf_d${ep_lib_suffix}
    IMPORTED_LOCATION_RELEASE
      ${BINARY_DIR}/${pbuf_lib_pre}protobuf${ep_lib_suffix}
    INTERFACE_INCLUDE_DIRECTORIES
      ${SOURCE_DIR}/src)
  add_dependencies (protobuf::libprotobuf protobuf_src)
  exclude_if_included (protobuf_src)
  exclude_if_included (protobuf::libprotobuf)

  if (NOT TARGET protobuf::protoc)
    add_executable (protobuf::protoc IMPORTED)
    exclude_if_included (protobuf::protoc)
  endif ()
  set_target_properties (protobuf::protoc PROPERTIES
    IMPORTED_LOCATION "${BINARY_DIR}/protoc${CMAKE_EXECUTABLE_SUFFIX}")
  add_dependencies (protobuf::protoc protobuf_src)
endif ()

file (MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/proto_gen)
set (save_CBD ${CMAKE_CURRENT_BINARY_DIR})
set (CMAKE_CURRENT_BINARY_DIR ${CMAKE_BINARY_DIR}/proto_gen)
protobuf_generate_cpp (
  PROTO_SRCS
  PROTO_HDRS
  src/ripple/proto/ripple.proto)
set (CMAKE_CURRENT_BINARY_DIR ${save_CBD})

add_library (pbufs STATIC ${PROTO_SRCS} ${PROTO_HDRS})

target_include_directories (pbufs PRIVATE src)
target_include_directories (pbufs
  SYSTEM PUBLIC ${CMAKE_BINARY_DIR}/proto_gen)
target_link_libraries (pbufs protobuf::libprotobuf)
target_compile_options (pbufs
  PUBLIC
    $<$<BOOL:${is_xcode}>:
      --system-header-prefix="google/protobuf"
      -Wno-deprecated-dynamic-exception-spec
    >)
add_library (Ripple::pbufs ALIAS pbufs)
target_link_libraries (ripple_libs INTERFACE Ripple::pbufs)
exclude_if_included (pbufs)

#[===================================================================[
   xrpl_core
   core functionality, useable by some client software perhaps
#]===================================================================]

file (GLOB_RECURSE rb_headers
  src/ripple/beast/*.h
  src/ripple/beast/*.hpp)

add_library (xrpl_core
  ${rb_headers}) ## headers added here for benefit of IDEs

#[===============================[
    beast/legacy FILES:
    TODO: review these sources for removal or replacement
#]===============================]
if (unity)
  target_sources (xrpl_core PRIVATE
    src/ripple/beast/core/core.unity.cpp
    src/ripple/beast/unity/beast_hash_unity.cpp
    src/ripple/beast/unity/beast_insight_unity.cpp
    src/ripple/beast/unity/beast_net_unity.cpp
    src/ripple/beast/unity/beast_utility_unity.cpp)
else ()
  target_sources (xrpl_core PRIVATE
    src/ripple/beast/core/CurrentThreadName.cpp
    src/ripple/beast/core/SemanticVersion.cpp
    src/ripple/beast/hash/impl/xxhash.cpp
    src/ripple/beast/insight/impl/Collector.cpp
    src/ripple/beast/insight/impl/Groups.cpp
    src/ripple/beast/insight/impl/Hook.cpp
    src/ripple/beast/insight/impl/Metric.cpp
    src/ripple/beast/insight/impl/NullCollector.cpp
    src/ripple/beast/insight/impl/StatsDCollector.cpp
    src/ripple/beast/net/impl/IPAddressConversion.cpp
    src/ripple/beast/net/impl/IPAddressV4.cpp
    src/ripple/beast/net/impl/IPAddressV6.cpp
    src/ripple/beast/net/impl/IPEndpoint.cpp
    src/ripple/beast/utility/src/beast_Journal.cpp
    src/ripple/beast/utility/src/beast_PropertyStream.cpp)
endif ()

#[===============================[
    core sources
#]===============================]
if (unity)
  target_sources (xrpl_core PRIVATE
    src/ripple/unity/basics1.cpp
    src/ripple/unity/json.cpp
    src/ripple/unity/protocol.cpp
    src/ripple/unity/crypto.cpp)
else ()
  target_sources (xrpl_core PRIVATE
    #[===============================[
      nounity, main sources:
        subdir: basics (partial)
    #]===============================]
    src/ripple/basics/impl/base64.cpp
    src/ripple/basics/impl/contract.cpp
    src/ripple/basics/impl/CountedObject.cpp
    src/ripple/basics/impl/FileUtilities.cpp
    src/ripple/basics/impl/Log.cpp
    src/ripple/basics/impl/strHex.cpp
    src/ripple/basics/impl/StringUtilities.cpp
    #[===============================[
      nounity, main sources:
        subdir: json
    #]===============================]
    src/ripple/json/impl/JsonPropertyStream.cpp
    src/ripple/json/impl/Object.cpp
    src/ripple/json/impl/Output.cpp
    src/ripple/json/impl/Writer.cpp
    src/ripple/json/impl/json_reader.cpp
    src/ripple/json/impl/json_value.cpp
    src/ripple/json/impl/json_valueiterator.cpp
    src/ripple/json/impl/json_writer.cpp
    src/ripple/json/impl/to_string.cpp
    #[===============================[
      nounity, main sources:
        subdir: protocol
    #]===============================]
    src/ripple/protocol/impl/AccountID.cpp
    src/ripple/protocol/impl/Book.cpp
    src/ripple/protocol/impl/BuildInfo.cpp
    src/ripple/protocol/impl/ErrorCodes.cpp
    src/ripple/protocol/impl/Feature.cpp
    src/ripple/protocol/impl/HashPrefix.cpp
    src/ripple/protocol/impl/IOUAmount.cpp
    src/ripple/protocol/impl/Indexes.cpp
    src/ripple/protocol/impl/InnerObjectFormats.cpp
    src/ripple/protocol/impl/Issue.cpp
    src/ripple/protocol/impl/Keylet.cpp
    src/ripple/protocol/impl/LedgerFormats.cpp
    src/ripple/protocol/impl/PublicKey.cpp
    src/ripple/protocol/impl/Quality.cpp
    src/ripple/protocol/impl/Rate2.cpp
    src/ripple/protocol/impl/SField.cpp
    src/ripple/protocol/impl/SOTemplate.cpp
    src/ripple/protocol/impl/STAccount.cpp
    src/ripple/protocol/impl/STAmount.cpp
    src/ripple/protocol/impl/STArray.cpp
    src/ripple/protocol/impl/STBase.cpp
    src/ripple/protocol/impl/STBlob.cpp
    src/ripple/protocol/impl/STInteger.cpp
    src/ripple/protocol/impl/STLedgerEntry.cpp
    src/ripple/protocol/impl/STObject.cpp
    src/ripple/protocol/impl/STParsedJSON.cpp
    src/ripple/protocol/impl/STPathSet.cpp
    src/ripple/protocol/impl/STTx.cpp
    src/ripple/protocol/impl/STValidation.cpp
    src/ripple/protocol/impl/STVar.cpp
    src/ripple/protocol/impl/STVector256.cpp
    src/ripple/protocol/impl/SecretKey.cpp
    src/ripple/protocol/impl/Seed.cpp
    src/ripple/protocol/impl/Serializer.cpp
    src/ripple/protocol/impl/Sign.cpp
    src/ripple/protocol/impl/TER.cpp
    src/ripple/protocol/impl/TxFormats.cpp
    src/ripple/protocol/impl/UintTypes.cpp
    src/ripple/protocol/impl/digest.cpp
    src/ripple/protocol/impl/tokens.cpp
    #[===============================[
      nounity, main sources:
        subdir: crypto
    #]===============================]
    src/ripple/crypto/impl/GenerateDeterministicKey.cpp
    src/ripple/crypto/impl/RFC1751.cpp
    src/ripple/crypto/impl/csprng.cpp
    src/ripple/crypto/impl/ec_key.cpp
    src/ripple/crypto/impl/openssl.cpp)
endif ()
add_library (Ripple::xrpl_core ALIAS xrpl_core)
target_include_directories (xrpl_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/ripple>
    # this one is for beast/legacy files:
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/beast/extras>
    $<INSTALL_INTERFACE:include>)
target_compile_options (xrpl_core
  PUBLIC
    $<$<BOOL:${is_gcc}>:-Wno-maybe-uninitialized>)
target_link_libraries (xrpl_core
  PUBLIC
    OpenSSL::Crypto
    NIH::secp256k1
    NIH::ed25519-donna
    Ripple::syslibs
    Ripple::boost
    Ripple::opts)
#[=================================[
   main/core headers installation
#]=================================]
install (
  FILES
    src/ripple/basics/base64.h
    src/ripple/basics/Blob.h
    src/ripple/basics/Buffer.h
    src/ripple/basics/CountedObject.h
    src/ripple/basics/FileUtilities.h
    src/ripple/basics/LocalValue.h
    src/ripple/basics/Log.h
    src/ripple/basics/safe_cast.h
    src/ripple/basics/Slice.h
    src/ripple/basics/StringUtilities.h
    src/ripple/basics/ToString.h
    src/ripple/basics/UnorderedContainers.h
    src/ripple/basics/algorithm.h
    src/ripple/basics/base_uint.h
    src/ripple/basics/chrono.h
    src/ripple/basics/contract.h
    src/ripple/basics/date.h
    src/ripple/basics/hardened_hash.h
    src/ripple/basics/strHex.h
  DESTINATION include/ripple/basics)
install (
  FILES
    src/ripple/crypto/GenerateDeterministicKey.h
    src/ripple/crypto/KeyType.h
    src/ripple/crypto/RFC1751.h
    src/ripple/crypto/csprng.h
  DESTINATION include/ripple/crypto)
install (
  FILES
    src/ripple/crypto/impl/ec_key.h
    src/ripple/crypto/impl/openssl.h
  DESTINATION include/ripple/crypto/impl)
install (
  FILES
    src/ripple/json/JsonPropertyStream.h
    src/ripple/json/Object.h
    src/ripple/json/Output.h
    src/ripple/json/Writer.h
    src/ripple/json/json_forwards.h
    src/ripple/json/json_reader.h
    src/ripple/json/json_value.h
    src/ripple/json/json_writer.h
    src/ripple/json/to_string.h
  DESTINATION include/ripple/json)
install (
  FILES
    src/ripple/json/impl/json_assert.h
  DESTINATION include/ripple/json/impl)
install (
  FILES
    src/ripple/protocol/AccountID.h
    src/ripple/protocol/AmountConversions.h
    src/ripple/protocol/Book.h
    src/ripple/protocol/BuildInfo.h
    src/ripple/protocol/ErrorCodes.h
    src/ripple/protocol/Feature.h
    src/ripple/protocol/HashPrefix.h
    src/ripple/protocol/IOUAmount.h
    src/ripple/protocol/Indexes.h
    src/ripple/protocol/InnerObjectFormats.h
    src/ripple/protocol/Issue.h
    src/ripple/protocol/Keylet.h
    src/ripple/protocol/KnownFormats.h
    src/ripple/protocol/LedgerFormats.h
    src/ripple/protocol/Protocol.h
    src/ripple/protocol/PublicKey.h
    src/ripple/protocol/Quality.h
    src/ripple/protocol/Rate.h
    src/ripple/protocol/SField.h
    src/ripple/protocol/SOTemplate.h
    src/ripple/protocol/STAccount.h
    src/ripple/protocol/STAmount.h
    src/ripple/protocol/STArray.h
    src/ripple/protocol/STBase.h
    src/ripple/protocol/STBitString.h
    src/ripple/protocol/STBlob.h
    src/ripple/protocol/STExchange.h
    src/ripple/protocol/STInteger.h
    src/ripple/protocol/STLedgerEntry.h
    src/ripple/protocol/STObject.h
    src/ripple/protocol/STParsedJSON.h
    src/ripple/protocol/STPathSet.h
    src/ripple/protocol/STTx.h
    src/ripple/protocol/STValidation.h
    src/ripple/protocol/STVector256.h
    src/ripple/protocol/SecretKey.h
    src/ripple/protocol/Seed.h
    src/ripple/protocol/Serializer.h
    src/ripple/protocol/Sign.h
    src/ripple/protocol/SystemParameters.h
    src/ripple/protocol/TER.h
    src/ripple/protocol/TxFlags.h
    src/ripple/protocol/TxFormats.h
    src/ripple/protocol/UintTypes.h
    src/ripple/protocol/XRPAmount.h
    src/ripple/protocol/digest.h
    src/ripple/protocol/jss.h
    src/ripple/protocol/tokens.h
  DESTINATION include/ripple/protocol)
install (
  FILES
    src/ripple/protocol/impl/STVar.h
    src/ripple/protocol/impl/secp256k1.h
  DESTINATION include/ripple/protocol/impl)

#[===================================[
   beast/legacy headers installation
#]===================================]
install (
  FILES
    src/ripple/beast/clock/abstract_clock.h
    src/ripple/beast/clock/basic_seconds_clock.h
    src/ripple/beast/clock/manual_clock.h
  DESTINATION include/ripple/beast/clock)
install (
  FILES
    src/ripple/beast/core/Config.h
    src/ripple/beast/core/ConfigCheck.h
    src/ripple/beast/core/LexicalCast.h
    src/ripple/beast/core/List.h
    src/ripple/beast/core/PlatformConfig.h
    src/ripple/beast/core/SemanticVersion.h
    src/ripple/beast/core/StandardConfig.h
  DESTINATION include/ripple/beast/core)
install (
  FILES
    src/ripple/beast/crypto/detail/mac_facade.h
    src/ripple/beast/crypto/detail/ripemd_context.h
    src/ripple/beast/crypto/detail/sha2_context.h
    src/ripple/beast/crypto/ripemd.h
    src/ripple/beast/crypto/secure_erase.h
    src/ripple/beast/crypto/sha2.h
  DESTINATION include/ripple/beast/crypto)
install (
  FILES
    src/ripple/beast/cxx17/type_traits.h
  DESTINATION include/ripple/beast/cxx17)
install (
  FILES
    src/ripple/beast/hash/endian.h
    src/ripple/beast/hash/hash_append.h
    src/ripple/beast/hash/meta.h
    src/ripple/beast/hash/uhash.h
    src/ripple/beast/hash/xxhasher.h
  DESTINATION include/ripple/beast/hash)
install (
  FILES src/ripple/beast/hash/impl/xxhash.h
  DESTINATION include/ripple/beast/hash/impl)
install (
  FILES
    src/ripple/beast/rfc2616.h
    src/ripple/beast/type_name.h
    src/ripple/beast/unit_test.h
    src/ripple/beast/xor_shift_engine.h
  DESTINATION include/ripple/beast)
install (
  FILES
    src/ripple/beast/utility/Journal.h
    src/ripple/beast/utility/PropertyStream.h
    src/ripple/beast/utility/Zero.h
    src/ripple/beast/utility/rngfill.h
  DESTINATION include/ripple/beast/utility)
# WARNING!! -- horrible levelization ahead
# (these files should be isolated or moved...but
#  unfortunately unit_test.h above creates this dependency)
install (
  FILES
    src/beast/extras/beast/unit_test/amount.hpp
    src/beast/extras/beast/unit_test/dstream.hpp
    src/beast/extras/beast/unit_test/global_suites.hpp
    src/beast/extras/beast/unit_test/match.hpp
    src/beast/extras/beast/unit_test/recorder.hpp
    src/beast/extras/beast/unit_test/reporter.hpp
    src/beast/extras/beast/unit_test/results.hpp
    src/beast/extras/beast/unit_test/runner.hpp
    src/beast/extras/beast/unit_test/suite.hpp
    src/beast/extras/beast/unit_test/suite_info.hpp
    src/beast/extras/beast/unit_test/suite_list.hpp
    src/beast/extras/beast/unit_test/thread.hpp
  DESTINATION include/beast/unit_test)
install (
  FILES
    src/beast/extras/beast/unit_test/detail/const_container.hpp
  DESTINATION include/beast/unit_test/detail)

#[===================================================================[
   rippled executable
#]===================================================================]

#[=========================================================[
   this one header is added as source just to keep older
   versions of cmake happy. cmake 3.10+ allows
   add_executable with no sources
#]=========================================================]
add_executable (rippled src/ripple/app/main/Application.h)
if (unity)
  target_sources (rippled PRIVATE
    #[===============================[
       unity, main sources
    #]===============================]
    src/ripple/unity/app_consensus.cpp
    src/ripple/unity/app_ledger.cpp
    src/ripple/unity/app_ledger_impl.cpp
    src/ripple/unity/app_main1.cpp
    src/ripple/unity/app_main2.cpp
    src/ripple/unity/app_misc.cpp
    src/ripple/unity/app_misc_impl.cpp
    src/ripple/unity/app_paths.cpp
    src/ripple/unity/app_tx.cpp
    src/ripple/unity/conditions.cpp
    src/ripple/unity/consensus.cpp
    src/ripple/unity/core.cpp
    src/ripple/unity/basics2.cpp
    src/ripple/unity/ledger.cpp
    src/ripple/unity/net.cpp
    src/ripple/unity/nodestore.cpp
    src/ripple/unity/overlay1.cpp
    src/ripple/unity/overlay2.cpp
    src/ripple/unity/peerfinder.cpp
    src/ripple/unity/resource.cpp
    src/ripple/unity/rpcx1.cpp
    src/ripple/unity/rpcx2.cpp
    src/ripple/unity/shamap.cpp
    src/ripple/unity/server.cpp
    src/ripple/unity/soci_ripple.cpp
    #[===============================[
       unity, test sources
    #]===============================]
    src/test/unity/app_test_unity1.cpp
    src/test/unity/app_test_unity2.cpp
    src/test/unity/basics_test_unity.cpp
    src/test/unity/beast_test_unity1.cpp
    src/test/unity/beast_test_unity2.cpp
    src/test/unity/conditions_test_unity.cpp
    src/test/unity/consensus_test_unity.cpp
    src/test/unity/core_test_unity.cpp
    src/test/unity/crypto_test_unity.cpp
    src/test/unity/json_test_unity.cpp
    src/test/unity/ledger_test_unity.cpp
    src/test/unity/nodestore_test_unity.cpp
    src/test/unity/overlay_test_unity.cpp
    src/test/unity/peerfinder_test_unity.cpp
    src/test/unity/protocol_test_unity.cpp
    src/test/unity/resource_test_unity.cpp
    src/test/unity/rpc_test_unity.cpp
    src/test/unity/server_test_unity.cpp
    src/test/unity/server_status_test_unity.cpp
    src/test/unity/shamap_test_unity.cpp
    src/test/unity/jtx_unity1.cpp
    src/test/unity/jtx_unity2.cpp
    src/test/unity/csf_unity.cpp)
else ()
  target_sources (rippled PRIVATE
    #[===============================[
       nounity, main sources:
         subdir: app
    #]===============================]
    src/ripple/app/consensus/RCLConsensus.cpp
    src/ripple/app/consensus/RCLCxPeerPos.cpp
    src/ripple/app/consensus/RCLValidations.cpp
    src/ripple/app/ledger/AcceptedLedger.cpp
    src/ripple/app/ledger/AcceptedLedgerTx.cpp
    src/ripple/app/ledger/AccountStateSF.cpp
    src/ripple/app/ledger/BookListeners.cpp
    src/ripple/app/ledger/ConsensusTransSetSF.cpp
    src/ripple/app/ledger/Ledger.cpp
    src/ripple/app/ledger/LedgerHistory.cpp
    src/ripple/app/ledger/OrderBookDB.cpp
    src/ripple/app/ledger/TransactionStateSF.cpp
    src/ripple/app/ledger/impl/BuildLedger.cpp
    src/ripple/app/ledger/impl/InboundLedger.cpp
    src/ripple/app/ledger/impl/InboundLedgers.cpp
    src/ripple/app/ledger/impl/InboundTransactions.cpp
    src/ripple/app/ledger/impl/LedgerCleaner.cpp
    src/ripple/app/ledger/impl/LedgerMaster.cpp
    src/ripple/app/ledger/impl/LedgerReplay.cpp
    src/ripple/app/ledger/impl/LedgerToJson.cpp
    src/ripple/app/ledger/impl/LocalTxs.cpp
    src/ripple/app/ledger/impl/OpenLedger.cpp
    src/ripple/app/ledger/impl/TransactionAcquire.cpp
    src/ripple/app/ledger/impl/TransactionMaster.cpp
    src/ripple/app/main/Application.cpp
    src/ripple/app/main/BasicApp.cpp
    src/ripple/app/main/CollectorManager.cpp
    src/ripple/app/main/DBInit.cpp
    src/ripple/app/main/LoadManager.cpp
    src/ripple/app/main/Main.cpp
    src/ripple/app/main/NodeIdentity.cpp
    src/ripple/app/main/NodeStoreScheduler.cpp
    src/ripple/app/misc/CanonicalTXSet.cpp
    src/ripple/app/misc/FeeVoteImpl.cpp
    src/ripple/app/misc/HashRouter.cpp
    src/ripple/app/misc/NetworkOPs.cpp
    src/ripple/app/misc/SHAMapStoreImp.cpp
    src/ripple/app/misc/impl/AccountTxPaging.cpp
    src/ripple/app/misc/impl/AmendmentTable.cpp
    src/ripple/app/misc/impl/LoadFeeTrack.cpp
    src/ripple/app/misc/impl/Manifest.cpp
    src/ripple/app/misc/impl/Transaction.cpp
    src/ripple/app/misc/impl/TxQ.cpp
    src/ripple/app/misc/impl/ValidatorKeys.cpp
    src/ripple/app/misc/impl/ValidatorList.cpp
    src/ripple/app/misc/impl/ValidatorSite.cpp
    src/ripple/app/paths/AccountCurrencies.cpp
    src/ripple/app/paths/Credit.cpp
    src/ripple/app/paths/Flow.cpp
    src/ripple/app/paths/Node.cpp
    src/ripple/app/paths/PathRequest.cpp
    src/ripple/app/paths/PathRequests.cpp
    src/ripple/app/paths/PathState.cpp
    src/ripple/app/paths/Pathfinder.cpp
    src/ripple/app/paths/RippleCalc.cpp
    src/ripple/app/paths/RippleLineCache.cpp
    src/ripple/app/paths/RippleState.cpp
    src/ripple/app/paths/cursor/AdvanceNode.cpp
    src/ripple/app/paths/cursor/DeliverNodeForward.cpp
    src/ripple/app/paths/cursor/DeliverNodeReverse.cpp
    src/ripple/app/paths/cursor/EffectiveRate.cpp
    src/ripple/app/paths/cursor/ForwardLiquidity.cpp
    src/ripple/app/paths/cursor/ForwardLiquidityForAccount.cpp
    src/ripple/app/paths/cursor/Liquidity.cpp
    src/ripple/app/paths/cursor/NextIncrement.cpp
    src/ripple/app/paths/cursor/ReverseLiquidity.cpp
    src/ripple/app/paths/cursor/ReverseLiquidityForAccount.cpp
    src/ripple/app/paths/cursor/RippleLiquidity.cpp
    src/ripple/app/paths/impl/BookStep.cpp
    src/ripple/app/paths/impl/DirectStep.cpp
    src/ripple/app/paths/impl/PaySteps.cpp
    src/ripple/app/paths/impl/XRPEndpointStep.cpp
    src/ripple/app/tx/impl/ApplyContext.cpp
    src/ripple/app/tx/impl/BookTip.cpp
    src/ripple/app/tx/impl/CancelCheck.cpp
    src/ripple/app/tx/impl/CancelOffer.cpp
    src/ripple/app/tx/impl/CancelTicket.cpp
    src/ripple/app/tx/impl/CashCheck.cpp
    src/ripple/app/tx/impl/Change.cpp
    src/ripple/app/tx/impl/CreateCheck.cpp
    src/ripple/app/tx/impl/CreateOffer.cpp
    src/ripple/app/tx/impl/CreateTicket.cpp
    src/ripple/app/tx/impl/DepositPreauth.cpp
    src/ripple/app/tx/impl/Escrow.cpp
    src/ripple/app/tx/impl/InvariantCheck.cpp
    src/ripple/app/tx/impl/OfferStream.cpp
    src/ripple/app/tx/impl/PayChan.cpp
    src/ripple/app/tx/impl/Payment.cpp
    src/ripple/app/tx/impl/SetAccount.cpp
    src/ripple/app/tx/impl/SetRegularKey.cpp
    src/ripple/app/tx/impl/SetSignerList.cpp
    src/ripple/app/tx/impl/SetTrust.cpp
    src/ripple/app/tx/impl/SignerEntries.cpp
    src/ripple/app/tx/impl/Taker.cpp
    src/ripple/app/tx/impl/Transactor.cpp
    src/ripple/app/tx/impl/apply.cpp
    src/ripple/app/tx/impl/applySteps.cpp
    #[===============================[
       nounity, main sources:
         subdir: basics (partial)
    #]===============================]
    src/ripple/basics/impl/Archive.cpp
    src/ripple/basics/impl/BasicConfig.cpp
    src/ripple/basics/impl/PerfLogImp.cpp
    src/ripple/basics/impl/ResolverAsio.cpp
    src/ripple/basics/impl/Sustain.cpp
    src/ripple/basics/impl/UptimeClock.cpp
    src/ripple/basics/impl/make_SSLContext.cpp
    src/ripple/basics/impl/mulDiv.cpp
    #[===============================[
       nounity, main sources:
         subdir: conditions
    #]===============================]
    src/ripple/conditions/impl/Condition.cpp
    src/ripple/conditions/impl/Fulfillment.cpp
    src/ripple/conditions/impl/error.cpp
    #[===============================[
       nounity, main sources:
         subdir: core
    #]===============================]
    src/ripple/core/impl/Config.cpp
    src/ripple/core/impl/DatabaseCon.cpp
    src/ripple/core/impl/Job.cpp
    src/ripple/core/impl/JobQueue.cpp
    src/ripple/core/impl/LoadEvent.cpp
    src/ripple/core/impl/LoadMonitor.cpp
    src/ripple/core/impl/SNTPClock.cpp
    src/ripple/core/impl/SociDB.cpp
    src/ripple/core/impl/Stoppable.cpp
    src/ripple/core/impl/TimeKeeper.cpp
    src/ripple/core/impl/Workers.cpp
    #[===============================[
       nounity, main sources:
         subdir: consensus
    #]===============================]
    src/ripple/consensus/Consensus.cpp
    #[===============================[
       nounity, main sources:
         subdir: ledger
    #]===============================]
    src/ripple/ledger/impl/ApplyStateTable.cpp
    src/ripple/ledger/impl/ApplyView.cpp
    src/ripple/ledger/impl/ApplyViewBase.cpp
    src/ripple/ledger/impl/ApplyViewImpl.cpp
    src/ripple/ledger/impl/BookDirs.cpp
    src/ripple/ledger/impl/CachedSLEs.cpp
    src/ripple/ledger/impl/CachedView.cpp
    src/ripple/ledger/impl/CashDiff.cpp
    src/ripple/ledger/impl/Directory.cpp
    src/ripple/ledger/impl/OpenView.cpp
    src/ripple/ledger/impl/PaymentSandbox.cpp
    src/ripple/ledger/impl/RawStateTable.cpp
    src/ripple/ledger/impl/ReadView.cpp
    src/ripple/ledger/impl/TxMeta.cpp
    src/ripple/ledger/impl/View.cpp
    #[===============================[
       nounity, main sources:
         subdir: net
    #]===============================]
    src/ripple/net/impl/HTTPClient.cpp
    src/ripple/net/impl/InfoSub.cpp
    src/ripple/net/impl/RPCCall.cpp
    src/ripple/net/impl/RPCErr.cpp
    src/ripple/net/impl/RPCSub.cpp
    src/ripple/net/impl/RegisterSSLCerts.cpp
    src/ripple/net/impl/SSLHTTPDownloader.cpp
    #[===============================[
       nounity, main sources:
         subdir: nodestore
    #]===============================]
    src/ripple/nodestore/backend/MemoryFactory.cpp
    src/ripple/nodestore/backend/NuDBFactory.cpp
    src/ripple/nodestore/backend/NullFactory.cpp
    src/ripple/nodestore/backend/RocksDBFactory.cpp
    src/ripple/nodestore/impl/BatchWriter.cpp
    src/ripple/nodestore/impl/Database.cpp
    src/ripple/nodestore/impl/DatabaseNodeImp.cpp
    src/ripple/nodestore/impl/DatabaseRotatingImp.cpp
    src/ripple/nodestore/impl/DatabaseShardImp.cpp
    src/ripple/nodestore/impl/DecodedBlob.cpp
    src/ripple/nodestore/impl/DummyScheduler.cpp
    src/ripple/nodestore/impl/EncodedBlob.cpp
    src/ripple/nodestore/impl/ManagerImp.cpp
    src/ripple/nodestore/impl/NodeObject.cpp
    src/ripple/nodestore/impl/Shard.cpp
    #[===============================[
       nounity, main sources:
         subdir: overlay
    #]===============================]
    src/ripple/overlay/impl/Cluster.cpp
    src/ripple/overlay/impl/ConnectAttempt.cpp
    src/ripple/overlay/impl/Message.cpp
    src/ripple/overlay/impl/OverlayImpl.cpp
    src/ripple/overlay/impl/PeerImp.cpp
    src/ripple/overlay/impl/PeerReservationTable.cpp
    src/ripple/overlay/impl/PeerSet.cpp
    src/ripple/overlay/impl/TMHello.cpp
    src/ripple/overlay/impl/TrafficCount.cpp
    #[===============================[
       nounity, main sources:
         subdir: peerfinder
    #]===============================]
    src/ripple/peerfinder/impl/Bootcache.cpp
    src/ripple/peerfinder/impl/Endpoint.cpp
    src/ripple/peerfinder/impl/PeerfinderConfig.cpp
    src/ripple/peerfinder/impl/PeerfinderManager.cpp
    src/ripple/peerfinder/impl/SlotImp.cpp
    src/ripple/peerfinder/impl/SourceStrings.cpp
    #[===============================[
       nounity, main sources:
         subdir: resource
    #]===============================]
    src/ripple/resource/impl/Charge.cpp
    src/ripple/resource/impl/Consumer.cpp
    src/ripple/resource/impl/Fees.cpp
    src/ripple/resource/impl/ResourceManager.cpp
    #[===============================[
       nounity, main sources:
         subdir: rpc
    #]===============================]
    src/ripple/rpc/handlers/AccountChannels.cpp
    src/ripple/rpc/handlers/AccountCurrenciesHandler.cpp
    src/ripple/rpc/handlers/AccountInfo.cpp
    src/ripple/rpc/handlers/AccountLines.cpp
    src/ripple/rpc/handlers/AccountObjects.cpp
    src/ripple/rpc/handlers/AccountOffers.cpp
    src/ripple/rpc/handlers/AccountTx.cpp
    src/ripple/rpc/handlers/AccountTxOld.cpp
    src/ripple/rpc/handlers/AccountTxSwitch.cpp
    src/ripple/rpc/handlers/BlackList.cpp
    src/ripple/rpc/handlers/BookOffers.cpp
    src/ripple/rpc/handlers/CanDelete.cpp
    src/ripple/rpc/handlers/Connect.cpp
    src/ripple/rpc/handlers/ConsensusInfo.cpp
    src/ripple/rpc/handlers/CrawlShards.cpp
    src/ripple/rpc/handlers/DepositAuthorized.cpp
    src/ripple/rpc/handlers/DownloadShard.cpp
    src/ripple/rpc/handlers/Feature1.cpp
    src/ripple/rpc/handlers/Fee1.cpp
    src/ripple/rpc/handlers/FetchInfo.cpp
    src/ripple/rpc/handlers/GatewayBalances.cpp
    src/ripple/rpc/handlers/GetCounts.cpp
    src/ripple/rpc/handlers/LedgerAccept.cpp
    src/ripple/rpc/handlers/LedgerCleanerHandler.cpp
    src/ripple/rpc/handlers/LedgerClosed.cpp
    src/ripple/rpc/handlers/LedgerCurrent.cpp
    src/ripple/rpc/handlers/LedgerData.cpp
    src/ripple/rpc/handlers/LedgerEntry.cpp
    src/ripple/rpc/handlers/LedgerHandler.cpp
    src/ripple/rpc/handlers/LedgerHeader.cpp
    src/ripple/rpc/handlers/LedgerRequest.cpp
    src/ripple/rpc/handlers/LogLevel.cpp
    src/ripple/rpc/handlers/LogRotate.cpp
    src/ripple/rpc/handlers/NoRippleCheck.cpp
    src/ripple/rpc/handlers/OwnerInfo.cpp
    src/ripple/rpc/handlers/PathFind.cpp
    src/ripple/rpc/handlers/PayChanClaim.cpp
    src/ripple/rpc/handlers/Peers.cpp
    src/ripple/rpc/handlers/Ping.cpp
    src/ripple/rpc/handlers/Print.cpp
    src/ripple/rpc/handlers/Random.cpp
    src/ripple/rpc/handlers/Reservations.cpp
    src/ripple/rpc/handlers/RipplePathFind.cpp
    src/ripple/rpc/handlers/ServerInfo.cpp
    src/ripple/rpc/handlers/ServerState.cpp
    src/ripple/rpc/handlers/SignFor.cpp
    src/ripple/rpc/handlers/SignHandler.cpp
    src/ripple/rpc/handlers/Stop.cpp
    src/ripple/rpc/handlers/Submit.cpp
    src/ripple/rpc/handlers/SubmitMultiSigned.cpp
    src/ripple/rpc/handlers/Subscribe.cpp
    src/ripple/rpc/handlers/TransactionEntry.cpp
    src/ripple/rpc/handlers/Tx.cpp
    src/ripple/rpc/handlers/TxHistory.cpp
    src/ripple/rpc/handlers/UnlList.cpp
    src/ripple/rpc/handlers/Unsubscribe.cpp
    src/ripple/rpc/handlers/ValidationCreate.cpp
    src/ripple/rpc/handlers/ValidatorListSites.cpp
    src/ripple/rpc/handlers/Validators.cpp
    src/ripple/rpc/handlers/WalletPropose.cpp
    src/ripple/rpc/impl/DeliveredAmount.cpp
    src/ripple/rpc/impl/Handler.cpp
    src/ripple/rpc/impl/LegacyPathFind.cpp
    src/ripple/rpc/impl/RPCHandler.cpp
    src/ripple/rpc/impl/RPCHelpers.cpp
    src/ripple/rpc/impl/Role.cpp
    src/ripple/rpc/impl/ServerHandlerImp.cpp
    src/ripple/rpc/impl/ShardArchiveHandler.cpp
    src/ripple/rpc/impl/Status.cpp
    src/ripple/rpc/impl/TransactionSign.cpp
    #[===============================[
       nounity, main sources:
         subdir: server
    #]===============================]
    src/ripple/server/impl/JSONRPCUtil.cpp
    src/ripple/server/impl/Port.cpp
    #[===============================[
       nounity, main sources:
         subdir: shamap
    #]===============================]
    src/ripple/shamap/impl/SHAMap.cpp
    src/ripple/shamap/impl/SHAMapDelta.cpp
    src/ripple/shamap/impl/SHAMapItem.cpp
    src/ripple/shamap/impl/SHAMapMissingNode.cpp
    src/ripple/shamap/impl/SHAMapNodeID.cpp
    src/ripple/shamap/impl/SHAMapSync.cpp
    src/ripple/shamap/impl/SHAMapTreeNode.cpp
    #[===============================[
       nounity, test sources:
         subdir: app
    #]===============================]
    src/test/app/AccountTxPaging_test.cpp
    src/test/app/AmendmentTable_test.cpp
    src/test/app/Check_test.cpp
    src/test/app/CrossingLimits_test.cpp
    src/test/app/DeliverMin_test.cpp
    src/test/app/DepositAuth_test.cpp
    src/test/app/Discrepancy_test.cpp
    src/test/app/Escrow_test.cpp
    src/test/app/Flow_test.cpp
    src/test/app/Freeze_test.cpp
    src/test/app/HashRouter_test.cpp
    src/test/app/LedgerHistory_test.cpp
    src/test/app/LedgerLoad_test.cpp
    src/test/app/LedgerReplay_test.cpp
    src/test/app/LoadFeeTrack_test.cpp
    src/test/app/Manifest_test.cpp
    src/test/app/MultiSign_test.cpp
    src/test/app/OfferStream_test.cpp
    src/test/app/Offer_test.cpp
    src/test/app/OversizeMeta_test.cpp
    src/test/app/Path_test.cpp
    src/test/app/PayChan_test.cpp
    src/test/app/PayStrand_test.cpp
    src/test/app/PseudoTx_test.cpp
    src/test/app/RCLCensorshipDetector_test.cpp
    src/test/app/RCLValidations_test.cpp
    src/test/app/Regression_test.cpp
    src/test/app/SHAMapStore_test.cpp
    src/test/app/SetAuth_test.cpp
    src/test/app/SetRegularKey_test.cpp
    src/test/app/SetTrust_test.cpp
    src/test/app/Taker_test.cpp
    src/test/app/Ticket_test.cpp
    src/test/app/Transaction_ordering_test.cpp
    src/test/app/TrustAndBalance_test.cpp
    src/test/app/TxQ_test.cpp
    src/test/app/ValidatorKeys_test.cpp
    src/test/app/ValidatorList_test.cpp
    src/test/app/ValidatorSite_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: basics
    #]===============================]
    src/test/basics/Buffer_test.cpp
    src/test/basics/DetectCrash_test.cpp
    src/test/basics/FileUtilities_test.cpp
    src/test/basics/KeyCache_test.cpp
    src/test/basics/PerfLog_test.cpp
    src/test/basics/RangeSet_test.cpp
    src/test/basics/Slice_test.cpp
    src/test/basics/StringUtilities_test.cpp
    src/test/basics/TaggedCache_test.cpp
    src/test/basics/base64_test.cpp
    src/test/basics/base_uint_test.cpp
    src/test/basics/contract_test.cpp
    src/test/basics/hardened_hash_test.cpp
    src/test/basics/mulDiv_test.cpp
    src/test/basics/qalloc_test.cpp
    src/test/basics/tagged_integer_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: beast
    #]===============================]
    src/test/beast/IPEndpoint_test.cpp
    src/test/beast/LexicalCast_test.cpp
    src/test/beast/SemanticVersion_test.cpp
    src/test/beast/aged_associative_container_test.cpp
    src/test/beast/beast_CurrentThreadName_test.cpp
    src/test/beast/beast_Debug_test.cpp
    src/test/beast/beast_Journal_test.cpp
    src/test/beast/beast_PropertyStream_test.cpp
    src/test/beast/beast_Zero_test.cpp
    src/test/beast/beast_abstract_clock_test.cpp
    src/test/beast/beast_asio_error_test.cpp
    src/test/beast/beast_basic_seconds_clock_test.cpp
    src/test/beast/beast_io_latency_probe_test.cpp
    src/test/beast/define_print.cpp
    #[===============================[
       nounity, test sources:
         subdir: conditions
    #]===============================]
    src/test/conditions/PreimageSha256_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: consensus
    #]===============================]
    src/test/consensus/ByzantineFailureSim_test.cpp
    src/test/consensus/Consensus_test.cpp
    src/test/consensus/DistributedValidatorsSim_test.cpp
    src/test/consensus/LedgerTiming_test.cpp
    src/test/consensus/LedgerTrie_test.cpp
    src/test/consensus/ScaleFreeSim_test.cpp
    src/test/consensus/Validations_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: core
    #]===============================]
    src/test/core/ClosureCounter_test.cpp
    src/test/core/Config_test.cpp
    src/test/core/Coroutine_test.cpp
    src/test/core/CryptoPRNG_test.cpp
    src/test/core/JobQueue_test.cpp
    src/test/core/SociDB_test.cpp
    src/test/core/Stoppable_test.cpp
    src/test/core/Workers_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: crypto
    #]===============================]
    src/test/crypto/Openssl_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: csf
    #]===============================]
    src/test/csf/BasicNetwork_test.cpp
    src/test/csf/Digraph_test.cpp
    src/test/csf/Histogram_test.cpp
    src/test/csf/Scheduler_test.cpp
    src/test/csf/impl/Sim.cpp
    src/test/csf/impl/ledgers.cpp
    #[===============================[
       nounity, test sources:
         subdir: json
    #]===============================]
    src/test/json/Object_test.cpp
    src/test/json/Output_test.cpp
    src/test/json/Writer_test.cpp
    src/test/json/json_value_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: jtx
    #]===============================]
    src/test/jtx/Env_test.cpp
    src/test/jtx/WSClient_test.cpp
    src/test/jtx/impl/Account.cpp
    src/test/jtx/impl/Env.cpp
    src/test/jtx/impl/JSONRPCClient.cpp
    src/test/jtx/impl/ManualTimeKeeper.cpp
    src/test/jtx/impl/WSClient.cpp
    src/test/jtx/impl/amount.cpp
    src/test/jtx/impl/balance.cpp
    src/test/jtx/impl/check.cpp
    src/test/jtx/impl/delivermin.cpp
    src/test/jtx/impl/deposit.cpp
    src/test/jtx/impl/envconfig.cpp
    src/test/jtx/impl/fee.cpp
    src/test/jtx/impl/flags.cpp
    src/test/jtx/impl/jtx_json.cpp
    src/test/jtx/impl/memo.cpp
    src/test/jtx/impl/multisign.cpp
    src/test/jtx/impl/offer.cpp
    src/test/jtx/impl/owners.cpp
    src/test/jtx/impl/paths.cpp
    src/test/jtx/impl/pay.cpp
    src/test/jtx/impl/quality2.cpp
    src/test/jtx/impl/rate.cpp
    src/test/jtx/impl/regkey.cpp
    src/test/jtx/impl/sendmax.cpp
    src/test/jtx/impl/seq.cpp
    src/test/jtx/impl/sig.cpp
    src/test/jtx/impl/tag.cpp
    src/test/jtx/impl/ticket.cpp
    src/test/jtx/impl/trust.cpp
    src/test/jtx/impl/txflags.cpp
    src/test/jtx/impl/utility.cpp
    #[===============================[
       nounity, test sources:
         subdir: ledger
    #]===============================]
    src/test/ledger/BookDirs_test.cpp
    src/test/ledger/CashDiff_test.cpp
    src/test/ledger/Directory_test.cpp
    src/test/ledger/Invariants_test.cpp
    src/test/ledger/PaymentSandbox_test.cpp
    src/test/ledger/PendingSaves_test.cpp
    src/test/ledger/SHAMapV2_test.cpp
    src/test/ledger/SkipList_test.cpp
    src/test/ledger/View_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: nodestore
    #]===============================]
    src/test/nodestore/Backend_test.cpp
    src/test/nodestore/Basics_test.cpp
    src/test/nodestore/Database_test.cpp
    src/test/nodestore/Timing_test.cpp
    src/test/nodestore/import_test.cpp
    src/test/nodestore/varint_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: overlay
    #]===============================]
    src/test/overlay/TMHello_test.cpp
    src/test/overlay/cluster_test.cpp
    src/test/overlay/short_read_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: peerfinder
    #]===============================]
    src/test/peerfinder/Livecache_test.cpp
    src/test/peerfinder/PeerFinder_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: protocol
    #]===============================]
    src/test/protocol/BuildInfo_test.cpp
    src/test/protocol/IOUAmount_test.cpp
    src/test/protocol/InnerObjectFormats_test.cpp
    src/test/protocol/Issue_test.cpp
    src/test/protocol/PublicKey_test.cpp
    src/test/protocol/Quality_test.cpp
    src/test/protocol/STAccount_test.cpp
    src/test/protocol/STAmount_test.cpp
    src/test/protocol/STObject_test.cpp
    src/test/protocol/STTx_test.cpp
    src/test/protocol/STValidation_test.cpp
    src/test/protocol/SecretKey_test.cpp
    src/test/protocol/Seed_test.cpp
    src/test/protocol/TER_test.cpp
    src/test/protocol/XRPAmount_test.cpp
    src/test/protocol/digest_test.cpp
    src/test/protocol/types_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: resource
    #]===============================]
    src/test/resource/Logic_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: rpc
    #]===============================]
    src/test/rpc/AccountCurrencies_test.cpp
    src/test/rpc/AccountInfo_test.cpp
    src/test/rpc/AccountLinesRPC_test.cpp
    src/test/rpc/AccountObjects_test.cpp
    src/test/rpc/AccountOffers_test.cpp
    src/test/rpc/AccountSet_test.cpp
    src/test/rpc/AccountTx_test.cpp
    src/test/rpc/AmendmentBlocked_test.cpp
    src/test/rpc/Book_test.cpp
    src/test/rpc/DepositAuthorized_test.cpp
    src/test/rpc/DeliveredAmount_test.cpp
    src/test/rpc/Feature_test.cpp
    src/test/rpc/GatewayBalances_test.cpp
    src/test/rpc/GetCounts_test.cpp
    src/test/rpc/JSONRPC_test.cpp
    src/test/rpc/KeyGeneration_test.cpp
    src/test/rpc/LedgerClosed_test.cpp
    src/test/rpc/LedgerData_test.cpp
    src/test/rpc/LedgerRPC_test.cpp
    src/test/rpc/LedgerRequestRPC_test.cpp
    src/test/rpc/NoRippleCheck_test.cpp
    src/test/rpc/NoRipple_test.cpp
    src/test/rpc/OwnerInfo_test.cpp
    src/test/rpc/Peers_test.cpp
    src/test/rpc/RPCCall_test.cpp
    src/test/rpc/RPCOverload_test.cpp
    src/test/rpc/RobustTransaction_test.cpp
    src/test/rpc/ServerInfo_test.cpp
    src/test/rpc/Status_test.cpp
    src/test/rpc/Subscribe_test.cpp
    src/test/rpc/TransactionEntry_test.cpp
    src/test/rpc/TransactionHistory_test.cpp
    src/test/rpc/ValidatorRPC_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: server
    #]===============================]
    src/test/server/ServerStatus_test.cpp
    src/test/server/Server_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: shamap
    #]===============================]
    src/test/shamap/FetchPack_test.cpp
    src/test/shamap/SHAMapSync_test.cpp
    src/test/shamap/SHAMap_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: unit_test
    #]===============================]
    src/test/unit_test/multi_runner.cpp)
endif ()
target_link_libraries (rippled
  Ripple::boost
  Ripple::opts
  Ripple::libs
  Ripple::xrpl_core)
exclude_if_included (rippled)
# define a macro for tests that might need to
# be exluded or run differently in CI environment
if (is_ci)
  target_compile_definitions(rippled PRIVATE RIPPLED_RUNNING_IN_CI)
endif ()


#[===================================================================[
   install stuff
#]===================================================================]

install (
  TARGETS
    secp256k1
    ed25519-donna
    common
    opts
    ripple_syslibs
    ripple_boost
    xrpl_core
  EXPORT RippleExports
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include)
install (EXPORT RippleExports
  FILE RippleTargets.cmake
  NAMESPACE Ripple::
  DESTINATION lib/cmake/ripple)
include (CMakePackageConfigHelpers)
write_basic_package_version_file (
  RippleConfigVersion.cmake
  VERSION ${rippled_version}
  COMPATIBILITY SameMajorVersion)

if (is_root_project)
  install (TARGETS rippled RUNTIME DESTINATION bin)
  set_target_properties(rippled PROPERTIES INSTALL_RPATH_USE_LINK_PATH ON)
  install (
    FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/Builds/CMake/RippleConfig.cmake
      ${CMAKE_CURRENT_BINARY_DIR}/RippleConfigVersion.cmake
    DESTINATION lib/cmake/ripple)
  # sample configs should not overwrite existing files
  # install if-not-exists workaround as suggested by
  # https://cmake.org/Bug/view.php?id=12646
  install(CODE "
    macro (copy_if_not_exists SRC DEST NEWNAME)
      if (NOT EXISTS \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/\${DEST}/\${NEWNAME}\")
        file (INSTALL DESTINATION \"\${CMAKE_INSTALL_PREFIX}/\${DEST}\" FILES \"\${SRC}\" RENAME \"\${NEWNAME}\")
      else ()
        message (\"-- Skipping : \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/\${DEST}/\${NEWNAME}\")
      endif ()
    endmacro()
    copy_if_not_exists(\"${CMAKE_CURRENT_SOURCE_DIR}/cfg/rippled-example.cfg\" etc rippled.cfg)
    copy_if_not_exists(\"${CMAKE_CURRENT_SOURCE_DIR}/cfg/validators-example.txt\" etc validators.txt)
  ")
endif ()

#[===================================================================[
   coverage report target
#]===================================================================]

if (coverage)
  if (is_clang)
    if (APPLE)
      execute_process (COMMAND xcrun -f llvm-profdata
        OUTPUT_VARIABLE LLVM_PROFDATA
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    else ()
      find_program (LLVM_PROFDATA llvm-profdata)
    endif ()
    if (NOT LLVM_PROFDATA)
      message (WARNING "unable to find llvm-profdata - skipping coverage_report target")
    endif ()

    if (APPLE)
      execute_process (COMMAND xcrun -f llvm-cov
        OUTPUT_VARIABLE LLVM_COV
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    else ()
      find_program (LLVM_COV llvm-cov)
    endif ()
    if (NOT LLVM_COV)
      message (WARNING "unable to find llvm-cov - skipping coverage_report target")
    endif ()

    set (extract_pattern "")
    if (coverage_core_only)
      set (extract_pattern "${CMAKE_SOURCE_DIR}/src/ripple/")
    endif ()

    if (LLVM_COV AND LLVM_PROFDATA)
      add_custom_target (coverage_report
        USES_TERMINAL
        COMMAND ${CMAKE_COMMAND} -E echo "Generating coverage - results will be in ${CMAKE_BINARY_DIR}/coverage/index.html."
        COMMAND ${CMAKE_COMMAND} -E echo "Running rippled tests."
        COMMAND rippled --unittest$<$<BOOL:${coverage_test}>:=${coverage_test}> --quiet --unittest-log
        COMMAND ${LLVM_PROFDATA}
          merge -sparse default.profraw -o rip.profdata
        COMMAND ${CMAKE_COMMAND} -E echo "Summary of coverage:"
        COMMAND ${LLVM_COV}
          report -instr-profile=rip.profdata
          $<TARGET_FILE:rippled> ${extract_pattern}
        # generate html report
        COMMAND ${LLVM_COV}
          show -format=html -output-dir=${CMAKE_BINARY_DIR}/coverage
          -instr-profile=rip.profdata
          $<TARGET_FILE:rippled> ${extract_pattern}
        BYPRODUCTS coverage/index.html)
    endif ()
  elseif (is_gcc)
    find_program (LCOV lcov)
    if (NOT LCOV)
      message (WARNING "unable to find lcov - skipping coverage_report target")
    endif ()

    find_program (GENHTML genhtml)
    if (NOT GENHTML)
      message (WARNING "unable to find genhtml - skipping coverage_report target")
    endif ()

    set (extract_pattern "*")
    if (coverage_core_only)
      set (extract_pattern "*/src/ripple/*")
    endif ()

    if (LCOV AND GENHTML)
      add_custom_target (coverage_report
        USES_TERMINAL
        COMMAND ${CMAKE_COMMAND} -E echo "Generating coverage- results will be in ${CMAKE_BINARY_DIR}/coverage/index.html."
        # create baseline info file
        COMMAND ${LCOV}
          --no-external -d "${CMAKE_SOURCE_DIR}" -c -d . -i -o baseline.info
          | grep -v "ignoring data for external file"
        # run tests
        COMMAND ${CMAKE_COMMAND} -E echo "Running rippled tests for coverage report."
        COMMAND rippled --unittest$<$<BOOL:${coverage_test}>:=${coverage_test}> --quiet --unittest-log
        # Create test coverage data file
        COMMAND ${LCOV}
          --no-external -d "${CMAKE_SOURCE_DIR}" -c -d . -o tests.info
          | grep -v "ignoring data for external file"
        # Combine baseline and test coverage data
        COMMAND ${LCOV}
          -a baseline.info -a tests.info -o lcov-all.info
        # extract our files
        COMMAND ${LCOV}
          -e lcov-all.info "${extract_pattern}" -o lcov.info
        COMMAND ${CMAKE_COMMAND} -E echo "Summary of coverage:"
        COMMAND ${LCOV} --summary lcov.info
        # generate HTML report
        COMMAND ${GENHTML}
          -o ${CMAKE_BINARY_DIR}/coverage lcov.info
        BYPRODUCTS coverage/index.html)
    endif ()
  endif ()
endif ()

#[===================================================================[
   multiconfig misc
#]===================================================================]

if (is_multiconfig)
  # This code finds all source files in the src subdirectory for inclusion
  # in the IDE file tree as non-compiled sources. Since this file list will
  # have some overlap with files we have already added to our targets to
  # be compiled, we explicitly remove any of these target source files from
  # this list.
  file (GLOB_RECURSE all_sources RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    CONFIGURE_DEPENDS
    src/*.* Builds/*.md docs/*.md src/*.md Builds/*.cmake)
  file(GLOB md_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
    *.md)
  LIST(APPEND all_sources ${md_files})
  foreach (_target secp256k1 ed25519-donna pbufs xrpl_core rippled)
    get_target_property (_type ${_target} TYPE)
    if(_type STREQUAL "INTERFACE_LIBRARY")
      continue()
    endif()
    get_target_property (_src ${_target} SOURCES)
    list (REMOVE_ITEM all_sources ${_src})
  endforeach ()
  target_sources (rippled PRIVATE ${all_sources})
  set_property (
    SOURCE ${all_sources}
    APPEND
    PROPERTY HEADER_FILE_ONLY true)
  if (MSVC)
    set_property(
      DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      PROPERTY VS_STARTUP_PROJECT rippled)
  endif ()

  group_sources(src)
  group_sources(docs)
  group_sources(Builds)
endif ()

#[===================================================================[
   docs target (optional)
#]===================================================================]

find_package (Doxygen)
if (TARGET Doxygen::doxygen)
  set (doc_srcs docs/source.dox)
  file (GLOB_RECURSE other_docs docs/*.md)
  list (APPEND doc_srcs "${other_docs}")
  # read the source config and make a modified one
  # that points the output files to our build directory
  file (READ "${CMAKE_CURRENT_SOURCE_DIR}/docs/source.dox" dox_content)
  string (REGEX REPLACE "[\t ]*OUTPUT_DIRECTORY[\t ]*=(.*)"
    "OUTPUT_DIRECTORY=${CMAKE_BINARY_DIR}\n\\1"
    new_config "${dox_content}")
  file (WRITE "${CMAKE_BINARY_DIR}/source.dox" "${new_config}")
  add_custom_target (docs
    COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_BINARY_DIR}/source.dox"
    BYPRODUCTS "${CMAKE_BINARY_DIR}/html_doc/index.html"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    SOURCES "${doc_srcs}")
  if (is_multiconfig)
    set_property (
      SOURCE ${doc_srcs}
      APPEND
      PROPERTY HEADER_FILE_ONLY
      true)
  endif ()
else ()
  message (STATUS "doxygen executable not found -- skipping docs target")
endif ()

