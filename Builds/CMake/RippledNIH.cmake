#[===================================================================[
   NIH prefix path..this is where we will download
   and build any ExternalProjects, and they will hopefully
   survive across build directory deletion (manual cleans)
#]===================================================================]

string (REGEX REPLACE "[ \\/%]+" "_" gen_for_path ${CMAKE_GENERATOR})
string (TOLOWER ${gen_for_path} gen_for_path)
# HACK: trying to shorten paths for windows CI (which hits 260 MAXPATH easily)
# @see:  https://issues.jenkins-ci.org/browse/JENKINS-38706?focusedCommentId=339847
string (REPLACE "visual_studio" "vs" gen_for_path ${gen_for_path})
if (NOT DEFINED NIH_CACHE_ROOT)
  if (DEFINED ENV{NIH_CACHE_ROOT})
    set (NIH_CACHE_ROOT $ENV{NIH_CACHE_ROOT})
  else ()
    set (NIH_CACHE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.nih_c")
  endif ()
endif ()
set (nih_cache_path
  "${NIH_CACHE_ROOT}/${gen_for_path}/${CMAKE_CXX_COMPILER_ID}_${CMAKE_CXX_COMPILER_VERSION}")
if (NOT is_multiconfig)
  set (nih_cache_path "${nih_cache_path}/${CMAKE_BUILD_TYPE}")
endif ()
if (NOT DEFINED nih_src_path)
  if (DEFINED ENV{NIH_SRC_PATH})
    set (nih_src_path $ENV{NIH_SRC_PATH}/src)
  else ()
    set (nih_src_path "${NIH_CACHE_ROOT}/src")
  endif ()
endif ()
if (NOT DEFINED nih_stamp_path)
  if (DEFINED ENV{NIH_STAMP_PATH})
    set (nih_stamp_path $ENV{NIH_STAMP_PATH}/stamp)
  elseif (DEFINED ENV{NIH_SRC_PATH})
    set (nih_stamp_path $ENV{NIH_SRC_PATH}/stamp)
  else ()
    set (nih_stamp_path "${nih_cache_path}/stamp")
  endif ()
endif ()
file(TO_CMAKE_PATH "${nih_cache_path}" nih_cache_path)
file(TO_CMAKE_PATH "${nih_src_path}" nih_src_path)
file(TO_CMAKE_PATH "${nih_stamp_path}" nih_stamp_path)
message(STATUS "NIH-EP cache path: ${nih_cache_path}")
message(STATUS "NIH-EP source path: ${nih_src_path}")
message(STATUS "NIH-EP stamp path: ${nih_stamp_path}")
## two convenience variables:
set (ep_lib_prefix ${CMAKE_STATIC_LIBRARY_PREFIX})
set (ep_lib_suffix ${CMAKE_STATIC_LIBRARY_SUFFIX})

# this is a setting for FetchContent and needs to be
# a cache variable
# https://cmake.org/cmake/help/latest/module/FetchContent.html#populating-the-content
set (FETCHCONTENT_BASE_DIR ${nih_cache_path} CACHE STRING "" FORCE)
