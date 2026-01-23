#[===================================================================[
   xrpld compile options/settings via an interface library
#]===================================================================]

include(CompilationEnv)

# Set defaults for optional variables to avoid uninitialized variable warnings
if(NOT DEFINED voidstar)
  set(voidstar OFF)
endif()

add_library (opts INTERFACE)
add_library (Xrpl::opts ALIAS opts)
target_compile_definitions (opts
  INTERFACE
    BOOST_ASIO_DISABLE_HANDLER_TYPE_REQUIREMENTS
    BOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT
    BOOST_CONTAINER_FWD_BAD_DEQUE
    HAS_UNCAUGHT_EXCEPTIONS=1
    $<$<BOOL:${boost_show_deprecated}>:
      BOOST_ASIO_NO_DEPRECATED
      BOOST_FILESYSTEM_NO_DEPRECATED
    >
    $<$<NOT:$<BOOL:${boost_show_deprecated}>>:
      BOOST_COROUTINES_NO_DEPRECATION_WARNING
      BOOST_BEAST_ALLOW_DEPRECATED
      BOOST_FILESYSTEM_DEPRECATED
    >
    $<$<BOOL:${beast_no_unit_test_inline}>:BEAST_NO_UNIT_TEST_INLINE=1>
    $<$<BOOL:${beast_disable_autolink}>:BEAST_DONT_AUTOLINK_TO_WIN32_LIBRARIES=1>
    $<$<BOOL:${single_io_service_thread}>:XRPL_SINGLE_IO_SERVICE_THREAD=1>
    $<$<BOOL:${voidstar}>:ENABLE_VOIDSTAR>)
target_compile_options (opts
  INTERFACE
    $<$<AND:$<BOOL:${is_gcc}>,$<COMPILE_LANGUAGE:CXX>>:-Wsuggest-override>
    $<$<BOOL:${is_gcc}>:-Wno-maybe-uninitialized>
    $<$<BOOL:${perf}>:-fno-omit-frame-pointer>
    $<$<BOOL:${profile}>:-pg>
    $<$<AND:$<BOOL:${is_gcc}>,$<BOOL:${profile}>>:-p>)

target_link_libraries (opts
  INTERFACE
    $<$<BOOL:${profile}>:-pg>
    $<$<AND:$<BOOL:${is_gcc}>,$<BOOL:${profile}>>:-p>)

if(jemalloc)
  find_package(jemalloc REQUIRED)
  target_compile_definitions(opts INTERFACE PROFILE_JEMALLOC)
  target_link_libraries(opts INTERFACE jemalloc::jemalloc)
endif ()

#[===================================================================[
   xrpld transitive library deps via an interface library
#]===================================================================]

add_library (xrpl_syslibs INTERFACE)
add_library (Xrpl::syslibs ALIAS xrpl_syslibs)
target_link_libraries (xrpl_syslibs
  INTERFACE
    $<$<BOOL:${is_msvc}>:
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
    $<$<NOT:$<BOOL:${is_msvc}>>:dl>
    $<$<NOT:$<OR:$<BOOL:${is_msvc}>,$<BOOL:${is_macos}>>>:rt>)

if (NOT is_msvc)
  set (THREADS_PREFER_PTHREAD_FLAG ON)
  find_package (Threads)
  target_link_libraries (xrpl_syslibs INTERFACE Threads::Threads)
endif ()

add_library (xrpl_libs INTERFACE)
add_library (Xrpl::libs ALIAS xrpl_libs)
target_link_libraries (xrpl_libs INTERFACE Xrpl::syslibs)
