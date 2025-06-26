#[===================================================================[
   Build binary packages
#]===================================================================]

if(NOT package)
  message(FATAL_ERROR "Packaging not enabled! Aborting")
else()
  set(tests ON)
  set(xrpld ON)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  message(STATUS "Debug packages will not be generated with Release build")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/package")
include(cpack-config)
