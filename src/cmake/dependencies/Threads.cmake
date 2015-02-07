set(Threads_FIND_QUIETLY TRUE)

find_package(Threads)
message(STATUS "X: ${Threads_FOUND}")
boost_external_report(Threads LIBRARIES)
