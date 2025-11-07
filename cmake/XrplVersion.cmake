#[===================================================================[
   read version from source
#]===================================================================]

file(STRINGS src/libxrpl/protocol/BuildInfo.cpp BUILD_INFO)
foreach(line_ ${BUILD_INFO})
  if(line_ MATCHES "versionString[ ]*=[ ]*\"(.+)\"")
    set(xrpld_version ${CMAKE_MATCH_1})
  endif()
endforeach()
if(xrpld_version)
  message(STATUS "xrpld version: ${xrpld_version}")
else()
  message(FATAL_ERROR "unable to determine xrpld version")
endif()
