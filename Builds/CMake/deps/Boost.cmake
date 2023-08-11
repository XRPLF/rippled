find_package(Boost 1.70 REQUIRED
  COMPONENTS
    chrono
    container
    context
    coroutine
    date_time
    filesystem
    program_options
    regex
    system
    thread
)

add_library(ripple_boost INTERFACE)
add_library(Ripple::boost ALIAS ripple_boost)
if(XCODE)
  target_include_directories(ripple_boost BEFORE INTERFACE ${Boost_INCLUDE_DIRS})
  target_compile_options(ripple_boost INTERFACE --system-header-prefix="boost/")
else()
  target_include_directories(ripple_boost SYSTEM BEFORE INTERFACE ${Boost_INCLUDE_DIRS})
endif()

target_link_libraries(ripple_boost
  INTERFACE
    Boost::boost
    Boost::chrono
    Boost::container
    Boost::coroutine
    Boost::date_time
    Boost::filesystem
    Boost::program_options
    Boost::regex
    Boost::system
    Boost::thread)
if(Boost_COMPILER)
  target_link_libraries(ripple_boost INTERFACE Boost::disable_autolinking)
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
