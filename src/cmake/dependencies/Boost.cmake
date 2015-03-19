set(Boost_FIND_QUIETLY TRUE)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
find_package(Boost 1.33.1 COMPONENTS date_time)

if (NOT Boost_date_time_FOUND)
  find_package(Boost 1.33.1)
endif()

set(Boost_RELEASE_VERSION
  "${Boost_MAJOR_VERSION}.${Boost_MINOR_VERSION}.${Boost_SUBMINOR_VERSION}")

boost_external_report(Boost RELEASE_VERSION INCLUDE_DIR LIBRARIES)
