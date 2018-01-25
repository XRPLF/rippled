# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#.rst:
# FindPostgreSQL
# --------------
#
# Find the PostgreSQL installation.
#
# This module defines
#
# ::
#
#   POSTGRESQL_LIBRARIES - the PostgreSQL libraries needed for linking
#   POSTGRESQL_INCLUDE_DIRS - the directories of the PostgreSQL headers
#   POSTGRESQL_LIBRARY_DIRS  - the link directories for PostgreSQL libraries
#   POSTGRESQL_VERSION_STRING - the version of PostgreSQL found (since CMake 2.8.8)

# ----------------------------------------------------------------------------
# History:
# This module is derived from the module originally found in the VTK source tree.
#
# ----------------------------------------------------------------------------
# Note:
# POSTGRESQL_ADDITIONAL_VERSIONS is a variable that can be used to set the
# version mumber of the implementation of PostgreSQL.
# In Windows the default installation of PostgreSQL uses that as part of the path.
# E.g C:\Program Files\PostgreSQL\8.4.
# Currently, the following version numbers are known to this module:
# "9.6" "9.5" "9.4" "9.3" "9.2" "9.1" "9.0" "8.4" "8.3" "8.2" "8.1" "8.0"
#
# To use this variable just do something like this:
# set(POSTGRESQL_ADDITIONAL_VERSIONS "9.2" "8.4.4")
# before calling find_package(PostgreSQL) in your CMakeLists.txt file.
# This will mean that the versions you set here will be found first in the order
# specified before the default ones are searched.
#
# ----------------------------------------------------------------------------
# You may need to manually set:
#  POSTGRESQL_INCLUDE_DIR  - the path to where the PostgreSQL include files are.
#  POSTGRESQL_LIBRARY_DIR  - The path to where the PostgreSQL library files are.
# If FindPostgreSQL.cmake cannot find the include files or the library files.
#
# ----------------------------------------------------------------------------
# The following variables are set if PostgreSQL is found:
#  POSTGRESQL_FOUND         - Set to true when PostgreSQL is found.
#  POSTGRESQL_INCLUDE_DIRS  - Include directories for PostgreSQL
#  POSTGRESQL_LIBRARY_DIRS  - Link directories for PostgreSQL libraries
#  POSTGRESQL_LIBRARIES     - The PostgreSQL libraries.
#
# ----------------------------------------------------------------------------
# If you have installed PostgreSQL in a non-standard location.
# (Please note that in the following comments, it is assumed that <Your Path>
# points to the root directory of the include directory of PostgreSQL.)
# Then you have three options.
# 1) After CMake runs, set POSTGRESQL_INCLUDE_DIR to <Your Path>/include and
#    POSTGRESQL_LIBRARY_DIR to wherever the library pq (or libpq in windows) is
# 2) Use CMAKE_INCLUDE_PATH to set a path to <Your Path>/PostgreSQL<-version>. This will allow find_path()
#    to locate POSTGRESQL_INCLUDE_DIR by utilizing the PATH_SUFFIXES option. e.g. In your CMakeLists.txt file
#    set(CMAKE_INCLUDE_PATH ${CMAKE_INCLUDE_PATH} "<Your Path>/include")
# 3) Set an environment variable called ${POSTGRESQL_ROOT} that points to the root of where you have
#    installed PostgreSQL, e.g. <Your Path>.
#
# ----------------------------------------------------------------------------

set(POSTGRESQL_INCLUDE_PATH_DESCRIPTION "top-level directory containing the PostgreSQL include directories. E.g /usr/local/include/PostgreSQL/8.4 or C:/Program Files/PostgreSQL/8.4/include")
set(POSTGRESQL_INCLUDE_DIR_MESSAGE "Set the POSTGRESQL_INCLUDE_DIR cmake cache entry to the ${POSTGRESQL_INCLUDE_PATH_DESCRIPTION}")
set(POSTGRESQL_LIBRARY_PATH_DESCRIPTION "top-level directory containing the PostgreSQL libraries.")
set(POSTGRESQL_LIBRARY_DIR_MESSAGE "Set the POSTGRESQL_LIBRARY_DIR cmake cache entry to the ${POSTGRESQL_LIBRARY_PATH_DESCRIPTION}")
set(POSTGRESQL_ROOT_DIR_MESSAGE "Set the POSTGRESQL_ROOT system variable to where PostgreSQL is found on the machine E.g C:/Program Files/PostgreSQL/8.4")


set(POSTGRESQL_KNOWN_VERSIONS ${POSTGRESQL_ADDITIONAL_VERSIONS}
    "9.6" "9.5" "9.4" "9.3" "9.2" "9.1" "9.0" "8.4" "8.3" "8.2" "8.1" "8.0")

# Define additional search paths for root directories.
set( POSTGRESQL_ROOT_DIRECTORIES
   ENV POSTGRESQL_ROOT
   ${POSTGRESQL_ROOT}
)
foreach(suffix ${POSTGRESQL_KNOWN_VERSIONS})
  if(WIN32)
    list(APPEND POSTGRESQL_LIBRARY_ADDITIONAL_SEARCH_SUFFIXES
        "PostgreSQL/${suffix}/lib")
    list(APPEND POSTGRESQL_INCLUDE_ADDITIONAL_SEARCH_SUFFIXES
        "PostgreSQL/${suffix}/include")
    list(APPEND POSTGRESQL_TYPE_ADDITIONAL_SEARCH_SUFFIXES
        "PostgreSQL/${suffix}/include/server")
  endif()
  if(UNIX)
    list(APPEND POSTGRESQL_LIBRARY_ADDITIONAL_SEARCH_SUFFIXES
        "pgsql-${suffix}/lib")
    list(APPEND POSTGRESQL_INCLUDE_ADDITIONAL_SEARCH_SUFFIXES
        "pgsql-${suffix}/include")
    list(APPEND POSTGRESQL_TYPE_ADDITIONAL_SEARCH_SUFFIXES
        "postgresql/${suffix}/server"
        "pgsql-${suffix}/include/server")
  endif()
endforeach()

#
# Look for an installation.
#
find_path(POSTGRESQL_INCLUDE_DIR
  NAMES libpq-fe.h
  PATHS
   # Look in other places.
   ${POSTGRESQL_ROOT_DIRECTORIES}
  PATH_SUFFIXES
    pgsql
    postgresql
    include
    ${POSTGRESQL_INCLUDE_ADDITIONAL_SEARCH_SUFFIXES}
  # Help the user find it if we cannot.
  DOC "The ${POSTGRESQL_INCLUDE_DIR_MESSAGE}"
)

# The PostgreSQL library.
set (POSTGRESQL_LIBRARY_TO_FIND pq)
# Setting some more prefixes for the library
set (POSTGRESQL_LIB_PREFIX "")
if ( WIN32 )
  set (POSTGRESQL_LIB_PREFIX ${POSTGRESQL_LIB_PREFIX} "lib")
  set (POSTGRESQL_LIBRARY_TO_FIND ${POSTGRESQL_LIB_PREFIX}${POSTGRESQL_LIBRARY_TO_FIND})
endif()

find_library(POSTGRESQL_LIBRARY
 NAMES ${POSTGRESQL_LIBRARY_TO_FIND}
 PATHS
   ${POSTGRESQL_ROOT_DIRECTORIES}
 PATH_SUFFIXES
   lib
   ${POSTGRESQL_LIBRARY_ADDITIONAL_SEARCH_SUFFIXES}
 # Help the user find it if we cannot.
 DOC "The ${POSTGRESQL_LIBRARY_DIR_MESSAGE}"
)
get_filename_component(POSTGRESQL_LIBRARY_DIR ${POSTGRESQL_LIBRARY} PATH)

if (POSTGRESQL_INCLUDE_DIR)
  # Some platforms include multiple pg_config.hs for multi-lib configurations
  # This is a temporary workaround.  A better solution would be to compile
  # a dummy c file and extract the value of the symbol.
  file(GLOB _PG_CONFIG_HEADERS "${POSTGRESQL_INCLUDE_DIR}/pg_config*.h")
  foreach(_PG_CONFIG_HEADER ${_PG_CONFIG_HEADERS})
    if(EXISTS "${_PG_CONFIG_HEADER}")
      file(STRINGS "${_PG_CONFIG_HEADER}" pgsql_version_str
           REGEX "^#define[\t ]+PG_VERSION[\t ]+\".*\"")
      if(pgsql_version_str)
        string(REGEX REPLACE "^#define[\t ]+PG_VERSION[\t ]+\"([^\"]*)\".*"
               "\\1" POSTGRESQL_VERSION_STRING "${pgsql_version_str}")
        break()
      endif()
    endif()
  endforeach()
  unset(pgsql_version_str)
endif()

# Did we find anything?
# <SOCI>
#include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
include(FindPackageHandleStandardArgs)
# </SOCI>
find_package_handle_standard_args(PostgreSQL
                                  REQUIRED_VARS POSTGRESQL_LIBRARY POSTGRESQL_INCLUDE_DIR
                                  VERSION_VAR POSTGRESQL_VERSION_STRING)
set(POSTGRESQL_FOUND  ${POSTGRESQL_FOUND})

# Now try to get the include and library path.
if(POSTGRESQL_FOUND)
  set(POSTGRESQL_INCLUDE_DIRS ${POSTGRESQL_INCLUDE_DIR})
  set(POSTGRESQL_LIBRARY_DIRS ${POSTGRESQL_LIBRARY_DIR})
  set(POSTGRESQL_LIBRARIES ${POSTGRESQL_LIBRARY})
  set(POSTGRESQL_VERSION ${POSTGRESQL_VERSION_STRING})
endif()

mark_as_advanced(POSTGRESQL_INCLUDE_DIR POSTGRESQL_LIBRARY)
