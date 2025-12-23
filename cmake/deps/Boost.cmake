find_package(Boost 1.82 REQUIRED
  COMPONENTS
    chrono
    container
    coroutine
    date_time
    filesystem
    json
    program_options
    regex
    system
    thread
)

add_library(xrpl_boost INTERFACE)
add_library(Xrpl::boost ALIAS xrpl_boost)

target_link_libraries(xrpl_boost
  INTERFACE
    Boost::headers
    Boost::chrono
    Boost::container
    Boost::coroutine
    Boost::date_time
    Boost::filesystem
    Boost::json
    Boost::process
    Boost::program_options
    Boost::regex
    Boost::system
    Boost::thread)
if(Boost_COMPILER)
  target_link_libraries(xrpl_boost INTERFACE Boost::disable_autolinking)
endif()
if(san AND is_clang)
  # TODO: gcc does not support -fsanitize-blacklist...can we do something else
  # for gcc ?
  if(NOT Boost_INCLUDE_DIRS AND TARGET Boost::headers)
    get_target_property(Boost_INCLUDE_DIRS Boost::headers INTERFACE_INCLUDE_DIRECTORIES)
  endif()
  message(STATUS "Adding [${Boost_INCLUDE_DIRS}] to sanitizer blacklist")
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/san_bl.txt "src:${Boost_INCLUDE_DIRS}/*")
  target_compile_options(opts
    INTERFACE
      # ignore boost headers for sanitizing
      -fsanitize-blacklist=${CMAKE_CURRENT_BINARY_DIR}/san_bl.txt)
endif()
