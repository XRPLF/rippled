set(ODBC_FIND_QUIETLY TRUE)

find_package(ODBC)

boost_external_report(ODBC INCLUDE_DIR LIBRARIES)
