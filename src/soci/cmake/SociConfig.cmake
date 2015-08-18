################################################################################
# SociConfig.cmake - CMake build configuration of SOCI library
################################################################################
# Copyright (C) 2010 Mateusz Loskot <mateusz@loskot.net>
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
################################################################################

include(CheckCXXSymbolExists)

if(WIN32)
  check_cxx_symbol_exists("_M_AMD64" "" SOCI_TARGET_ARCH_X64)
  if(NOT RTC_ARCH_X64)
    check_cxx_symbol_exists("_M_IX86" "" SOCI_TARGET_ARCH_X86)
  endif(NOT RTC_ARCH_X64)
  # add check for arm here
  # see http://msdn.microsoft.com/en-us/library/b0084kay.aspx
else(WIN32)
  check_cxx_symbol_exists("__i386__" "" SOCI_TARGET_ARCH_X86)
  check_cxx_symbol_exists("__x86_64__" "" SOCI_TARGET_ARCH_X64)
  check_cxx_symbol_exists("__arm__" "" SOCI_TARGET_ARCH_ARM)
endif(WIN32)

#
# C++11 Option
#

set (SOCI_CXX_C11 OFF CACHE BOOL "Build to the C++11 standard")


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
	"-pedantic -Werror -Wall -Wpointer-arith -Wcast-align -Wcast-qual -Wfloat-equal -Wredundant-decls -Wno-long-long")


  if (SOCI_CXX_C11)
    set(SOCI_CXX_VERSION_FLAGS "-std=c++11")
    add_definitions(-DSOCI_CXX_C11)
  else()
    set(SOCI_CXX_VERSION_FLAGS "-std=gnu++98")
  endif()

  if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC ${SOCI_GCC_CLANG_COMMON_FLAGS} ${SOCI_CXX_VERSION_FLAGS} ")
    if (CMAKE_COMPILER_IS_GNUCXX)
        if (CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        else()
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-variadic-macros")
        endif()
    endif()

  elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang" OR "${CMAKE_CXX_COMPILER}" MATCHES "clang")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SOCI_GCC_CLANG_COMMON_FLAGS} ${SOCI_CXX_VERSION_FLAGS}")

  else()
	message(WARNING "Unknown toolset - using default flags to build SOCI")
  endif()

endif()
