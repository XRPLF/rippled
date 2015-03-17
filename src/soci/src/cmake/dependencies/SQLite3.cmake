set(SQLITE3_FIND_QUIETLY TRUE)

find_package(SQLite3)

boost_external_report(SQLite3 INCLUDE_DIR LIBRARIES)
