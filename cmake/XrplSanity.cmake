#[===================================================================[
   sanity checks
#]===================================================================]

include(CompilationEnv)

get_property(is_multiconfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

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

if (is_clang) # both Clang and AppleClang
  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" AND
         CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16.0)
    message (FATAL_ERROR "This project requires clang 16 or later")
  endif ()
elseif (is_gcc)
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12.0)
    message (FATAL_ERROR "This project requires GCC 12 or later")
  endif ()
endif ()

# check for in-source build and fail
if ("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
  message (FATAL_ERROR "Builds (in-source) are not allowed in "
    "${CMAKE_CURRENT_SOURCE_DIR}. Please remove CMakeCache.txt and the CMakeFiles "
    "directory from ${CMAKE_CURRENT_SOURCE_DIR} and try building in a separate directory.")
endif ()

if (MSVC AND CMAKE_GENERATOR_PLATFORM STREQUAL "Win32")
  message (FATAL_ERROR "Visual Studio 32-bit build is not supported.")
endif ()

if (APPLE AND NOT HOMEBREW)
  find_program (HOMEBREW brew)
endif ()
