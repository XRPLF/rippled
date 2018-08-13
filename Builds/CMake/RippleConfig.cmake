include (CMakeFindDependencyMacro)
# need to represent system dependencies of the lib here
#[=========================================================[
  Boost
#]=========================================================]
if (static OR APPLE OR MSVC)
  set (Boost_USE_STATIC_LIBS ON)
endif ()
set (Boost_USE_MULTITHREADED ON)
if (static OR MSVC)
  set (Boost_USE_STATIC_RUNTIME ON)
else ()
  set (Boost_USE_STATIC_RUNTIME OFF)
endif ()
find_dependency (Boost 1.67
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
#[=========================================================[
  OpenSSL
#]=========================================================]
if (NOT DEFINED OPENSSL_ROOT_DIR)
  if (DEFINED ENV{OPENSSL_ROOT})
    set (OPENSSL_ROOT_DIR $ENV{OPENSSL_ROOT})
  elseif (APPLE)
    find_program (homebrew brew)
    if (homebrew)
      execute_process (COMMAND ${homebrew} --prefix openssl
        OUTPUT_VARIABLE OPENSSL_ROOT_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif ()
  endif ()
  file (TO_CMAKE_PATH "${OPENSSL_ROOT_DIR}" OPENSSL_ROOT_DIR)
endif ()

if (static OR APPLE OR MSVC)
  set (OPENSSL_USE_STATIC_LIBS ON)
endif ()
set (OPENSSL_MSVC_STATIC_RT ON)
find_dependency (OpenSSL 1.0.2 REQUIRED)
find_dependency (ZLIB)
if (TARGET ZLIB::ZLIB)
  set_target_properties(OpenSSL::Crypto PROPERTIES
    INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)
endif ()

include ("${CMAKE_CURRENT_LIST_DIR}/RippleTargets.cmake")
