# 
# Find the ODBC driver manager includes and library.
# 
# ODBC is an open standard for connecting to different databases in a
# semi-vendor-independent fashion.  First you install the ODBC driver
# manager.  Then you need a driver for each separate database you want
# to connect to (unless a generic one works).  VTK includes neither
# the driver manager nor the vendor-specific drivers: you have to find
# those yourself.
#  
# This module defines
# ODBC_INCLUDE_DIR, where to find sql.h
# ODBC_LIBRARIES, the libraries to link against to use ODBC
# ODBC_FOUND.  If false, you cannot build anything that requires MySQL.

# also defined, but not for general use is
# ODBC_LIBRARY, where to find the ODBC driver manager library.

set(ODBC_FOUND FALSE)

find_path(ODBC_INCLUDE_DIR sql.h
  /usr/include
  /usr/include/odbc
  /usr/local/include
  /usr/local/include/odbc
  /usr/local/odbc/include
  "C:/Program Files (x86)/Windows Kits/8.0/include/um"
  "C:/Program Files (x86)/Microsoft SDKs/Windows/v7.0A/Include"
  "C:/Program Files/ODBC/include"
  "C:/Program Files/Microsoft SDKs/Windows/v7.0/include" 
  "C:/Program Files/Microsoft SDKs/Windows/v6.0a/include" 
  "C:/ODBC/include"
  DOC "Specify the directory containing sql.h."
)

find_library(ODBC_LIBRARY 
  NAMES iodbc odbc odbcinst odbc32
  PATHS
  /usr/lib
  /usr/lib/odbc
  /usr/local/lib
  /usr/local/lib/odbc
  /usr/local/odbc/lib
  "C:/Program Files (x86)/Windows Kits/8.0/Lib/win8/um/x86/"
  "C:/Program Files (x86)/Microsoft SDKs/Windows/v7.0A/Lib"
  "C:/Program Files/ODBC/lib"
  "C:/ODBC/lib/debug"
  DOC "Specify the ODBC driver manager library here."
)

if(ODBC_LIBRARY)
  if(ODBC_INCLUDE_DIR)
    set( ODBC_FOUND 1 )
  endif()
endif()

set(ODBC_LIBRARIES ${ODBC_LIBRARY})

mark_as_advanced(ODBC_FOUND ODBC_LIBRARY ODBC_EXTRA_LIBRARIES ODBC_INCLUDE_DIR)

