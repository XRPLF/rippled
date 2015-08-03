set(DB2_FIND_QUIETLY TRUE)

find_package(DB2)

boost_external_report(DB2 INCLUDE_DIR LIBRARIES)
