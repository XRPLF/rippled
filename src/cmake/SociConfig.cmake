################################################################################
# SociConfig.cmake - CMake build configuration of SOCI library
################################################################################
# Copyright (C) 2010 Mateusz Loskot <mateusz@loskot.net>
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
################################################################################

#
# Force compilation flags and set desired warnings level
#

if (MSVC)
  if (MSVC80 OR MSVC90 OR MSVC10)
    add_definitions(-D_CRT_SECURE_NO_DEPRECATE)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-D_CRT_NONSTDC_NO_WARNING)
    add_definitions(-D_SCL_SECURE_NO_WARNINGS)
  endif()
  
  if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  endif()
  
else()

  set(SOCI_GCC_CLANG_COMMON_FLAGS
	"-pedantic -ansi -Wall -Wpointer-arith -Wcast-align -Wcast-qual -Wfloat-equal -Wredundant-decls -Wno-long-long")

  if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
        
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC ${SOCI_GCC_CLANG_COMMON_FLAGS}")
    if (CMAKE_COMPILER_IS_GNUCXX)
        if (CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++98")
        else()
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++98")
        endif()
    endif()

  elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang" OR "${CMAKE_CXX_COMPILER}" MATCHES "clang")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SOCI_GCC_CLANG_COMMON_FLAGS}")

  else()
	message(FATAL_ERROR "CMake is unable to recognize compilation toolset to build SOCI for you!")
  endif()

endif()
