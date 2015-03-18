##############################################################
# Copyright (c) 2008 Daniel Pfeifer                          #
#                                                            #
# Distributed under the Boost Software License, Version 1.0. #
##############################################################

# This module defines
# FIREBIRD_INCLUDE_DIR - where to find ibase.h
# FIREBIRD_LIBRARIES - the libraries to link against to use FIREBIRD
# FIREBIRD_FOUND - true if FIREBIRD was found

find_path(FIREBIRD_INCLUDE_DIR ibase.h
  /usr/include
  $ENV{ProgramFiles}/Firebird/*/include
)

find_library(FIREBIRD_LIBRARIES
  NAMES
    fbclient
    fbclient_ms
  PATHS
    /usr/lib
    $ENV{ProgramFiles}/Firebird/*/lib
)

# fbembed ?

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Firebird
  DEFAULT_MSG FIREBIRD_LIBRARIES FIREBIRD_INCLUDE_DIR)

mark_as_advanced(FIREBIRD_INCLUDE_DIR FIREBIRD_LIBRARIES)

