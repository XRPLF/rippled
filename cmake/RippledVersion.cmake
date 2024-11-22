#[===================================================================[
   read version from source
#]===================================================================]

file(STRINGS src/libxrpl/protocol/BuildInfo.cpp BUILD_INFO)
foreach(line_ ${BUILD_INFO})
  if(line_ MATCHES "versionString[ ]*=[ ]*\"(.+)\"")
    set(rippled_version ${CMAKE_MATCH_1})
  endif()
endforeach()
if(rippled_version)
  message(STATUS "rippled version: ${rippled_version}")
else()
  message(FATAL_ERROR "unable to determine rippled version")
endif()
