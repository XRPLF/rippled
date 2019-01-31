set (THIRDPARTY_LIBS "")
if(WITH_SNAPPY)
  find_package(snappy REQUIRED)
  add_definitions(-DSNAPPY)
  include_directories(${SNAPPY_INCLUDE_DIR})
  list(APPEND THIRDPARTY_LIBS ${SNAPPY_LIBRARIES})
endif()

if(WITH_LZ4)
  find_package(lz4 REQUIRED)
  add_definitions(-DLZ4)
  include_directories(${LZ4_INCLUDE_DIR})
  list(APPEND THIRDPARTY_LIBS ${LZ4_LIBRARIES})
endif()


