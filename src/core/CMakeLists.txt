###############################################################################
#
# This file is part of CMake configuration for SOCI library
#
# Copyright (C) 2009-2013 Mateusz Loskot <mateusz@loskot.net>
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
#
###############################################################################
colormsg(_HIBLUE_ "Configuring SOCI core library:")

# Set INCLUDE_DIRECTORIES
get_directory_property(SOCI_CORE_INCLUDE_DIRS INCLUDE_DIRECTORIES)
list(APPEND SOCI_CORE_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR})
set_directory_properties(PROPERTIES
  INCLUDE_DIRECTORIES "${SOCI_CORE_INCLUDE_DIRS}")

# Configure backend loader to also use default install directory.
configure_file(soci_backends_config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/soci_backends_config.h)

# Core source files
file(GLOB SOCI_CORE_HEADERS ${SOCI_SOURCE_DIR}/include/soci/*.h)
file(GLOB SOCI_CORE_SOURCES *.cpp)

# Group source files for IDE source explorers (e.g. Visual Studio)
source_group("Header Files" FILES ${SOCI_CORE_HEADERS})
source_group("Source Files" FILES ${SOCI_CORE_SOURCES})
source_group("CMake Files" FILES CMakeLists.txt)

# Core targets configuration
string(TOLOWER "${PROJECT_NAME}" PROJECTNAMEL)
#this command will update parent scope variable
set(SOCI_CORE_TARGET ${PROJECTNAMEL}_core PARENT_SCOPE)
set(SOCI_CORE_TARGET ${PROJECTNAMEL}_core)

soci_target_output_name(${SOCI_CORE_TARGET} SOCI_CORE_TARGET_OUTPUT_NAME)

#
# Core shared library
#
if (SOCI_SHARED)
  add_library(${SOCI_CORE_TARGET} SHARED ${SOCI_CORE_HEADERS} ${SOCI_CORE_SOURCES})

  target_link_libraries(${SOCI_CORE_TARGET} ${SOCI_CORE_DEPS_LIBS})

  if(WIN32)
    set_target_properties(${SOCI_CORE_TARGET}
      PROPERTIES
      DEFINE_SYMBOL SOCI_DLL
      OUTPUT_NAME "${SOCI_CORE_TARGET_OUTPUT_NAME}"
      VERSION ${SOCI_VERSION}
      CLEAN_DIRECT_OUTPUT 1)
  else()
    set_target_properties(${SOCI_CORE_TARGET}
      PROPERTIES
      VERSION ${SOCI_VERSION}
      SOVERSION ${SOCI_SOVERSION}
      INSTALL_NAME_DIR ${CMAKE_INSTALL_PREFIX}/lib
      CLEAN_DIRECT_OUTPUT 1)
  endif()

  target_include_directories(${SOCI_CORE_TARGET}
      PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
  )

endif()

# This adds definitions to all build configurations. SOCI_DEBUG_POSTFIX is passed to soci library
add_definitions(-DSOCI_LIB_PREFIX="${CMAKE_SHARED_LIBRARY_PREFIX}soci_"
                -DSOCI_LIB_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}"
                -DSOCI_DEBUG_POSTFIX="${CMAKE_DEBUG_POSTFIX}")

#
# Core static library
#
if (SOCI_STATIC)
  set(SOCI_CORE_TARGET_STATIC ${SOCI_CORE_TARGET}_static)

  add_library(${SOCI_CORE_TARGET_STATIC} STATIC
    ${SOCI_CORE_HEADERS} ${SOCI_CORE_SOURCES})

  # we still need to link against dl if we have it
  target_link_libraries (${SOCI_CORE_TARGET_STATIC}
    ${SOCI_CORE_DEPS_LIBS}
  )

  set_target_properties(${SOCI_CORE_TARGET_STATIC}
    PROPERTIES
    OUTPUT_NAME ${SOCI_CORE_TARGET_OUTPUT_NAME}
    PREFIX "lib"
    CLEAN_DIRECT_OUTPUT 1)

  target_include_directories(${SOCI_CORE_TARGET_STATIC}
      PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
  )

endif()




#
# Core installation
#
install(FILES ${SOCI_CORE_HEADERS} DESTINATION ${INCLUDEDIR}/${PROJECTNAMEL})

if (SOCI_SHARED)
  install(TARGETS ${SOCI_CORE_TARGET}
    EXPORT SOCI
    RUNTIME DESTINATION ${BINDIR}
    LIBRARY DESTINATION ${LIBDIR}
    ARCHIVE DESTINATION ${LIBDIR})
endif()

if (SOCI_STATIC)
  install(TARGETS ${SOCI_CORE_TARGET_STATIC}
    EXPORT SOCI
    RUNTIME DESTINATION ${BINDIR}
    LIBRARY DESTINATION ${LIBDIR}
    ARCHIVE DESTINATION ${LIBDIR})
endif()

install(EXPORT SOCI NAMESPACE SOCI:: DESTINATION cmake)

#
# Core configuration summary
#
boost_report_value(SOCI_CORE_TARGET)
boost_report_value(SOCI_CORE_TARGET_OUTPUT_NAME)
boost_report_value(SOCI_CORE_DEPS_LIBS)
boost_report_value(SOCI_CORE_INCLUDE_DIRS)
boost_report_value(WITH_BOOST)
soci_report_directory_property(COMPILE_DEFINITIONS)

message(STATUS "")
