set(Firebird_FIND_QUIETLY TRUE)

find_package(Firebird)

boost_external_report(Firebird INCLUDE_DIR LIBRARIES VERSION)

