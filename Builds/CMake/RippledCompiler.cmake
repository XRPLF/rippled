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

  foreach (var_
      CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
      CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE)

    # also remove dynamic runtime
    string (REGEX REPLACE "[-/]MD[d]*" " " ${var_} "${${var_}}")

    # /ZI (Edit & Continue debugging information) is incompatible with Gy-
    string (REPLACE "/ZI" "/Zi" ${var_} "${${var_}}")
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
      WIN32_LEAN_AND_MEAN
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
  if (wextra)
    string (APPEND CMAKE_CXX_FLAGS " -Wextra -Wno-unused-parameter")
  endif ()
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
