set(ORACLE_FIND_QUIETLY TRUE)

find_package(Oracle)

boost_external_report(Oracle INCLUDE_DIR LIBRARIES)
