option(SOCI_FIREBIRD_EMBEDDED "Use embedded library in Firebird backend" OFF)
boost_report_value(SOCI_FIREBIRD_EMBEDDED)

set(Firebird_FIND_QUIETLY TRUE)

find_package(Firebird)

boost_external_report(Firebird INCLUDE_DIR LIBRARIES VERSION)

