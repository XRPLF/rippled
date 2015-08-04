set(Threads_FIND_QUIETLY TRUE)

find_package(Threads)
boost_report_value(CMAKE_THREAD_LIBS_INIT)
