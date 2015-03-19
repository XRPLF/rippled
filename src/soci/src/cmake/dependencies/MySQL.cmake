set(MySQL_FIND_QUIETLY TRUE)

find_package(MySQL)

boost_external_report(MySQL INCLUDE_DIR LIBRARIES)

#if(MYSQL_FOUND)
#  include_directories(${MYSQL_INCLUDE_DIR})
#  add_definitions(-DHAVE_MYSQL)
#endif()