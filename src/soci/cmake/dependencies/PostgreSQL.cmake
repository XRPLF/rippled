set(PostgreSQL_FIND_QUIETLY TRUE)

find_package(PostgreSQL)

boost_external_report(PostgreSQL INCLUDE_DIRS LIBRARIES VERSION)
