################################################################################
# SociSystemInfo.cmake - part of CMake configuration of SOCI library
#
# Based on idea taken from http://code.google.com/p/softart/ project
################################################################################
# Copyright (C) 2010 Mateusz Loskot <mateusz@loskot.net>
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
################################################################################
# The following variables are defined:
#   SOCI_COMPILER_NAME - name of compiler toolset, follows Boost toolset naming.
#   SOCI_PLATFORM_NAME - target platform name: x64, x86 or win32
#
# Based on the Pre-defined Compiler Macros 
# http://sourceforge.net/p/predef/wiki/Compilers/
################################################################################

set(SOCI_COMPILER_NAME)
set(SOCI_PLATFORM_NAME)

if(MINGW OR UNIX)
  exec_program(${CMAKE_C_COMPILER} ARGS -dumpversion OUTPUT_VARIABLE GCC_VERSION)
  string(REPLACE "." "" GCC_VERSION_STR_FULL ${GCC_VERSION})
  string(REGEX MATCH "[0-9]+\\.[0-9]+" GCC_VERSION_MAJOR_MINOR ${GCC_VERSION})
endif()

if(WIN32)
  if(MSVC)
    if(MSVC_VERSION EQUAL 1200)
      set(SOCI_COMPILER_NAME "msvc-6.0")
    endif()
    if(MSVC_VERSION EQUAL 1300)
      set(SOCI_COMPILER_NAME "msvc-7.0")
    endif()
    if(MSVC_VERSION EQUAL 1310)
      set(SOCI_COMPILER_NAME "msvc-7.1") # Visual Studio 2003
    endif()
    if(MSVC_VERSION EQUAL 1400)
      set(SOCI_COMPILER_NAME "msvc-8.0") # Visual Studio 2005
    endif()
    if(MSVC_VERSION EQUAL 1500)
      set(SOCI_COMPILER_NAME "msvc-9.0") # Visual Studio 2008
    endif()
    if(MSVC_VERSION EQUAL 1600)
      set(SOCI_COMPILER_NAME "msvc-10.0") # Visual Studio 2010
    endif()
    if(MSVC_VERSION EQUAL 1700)
      set(SOCI_COMPILER_NAME "msvc-11.0") # Visual Studio 2012
    endif()
  endif(MSVC)
  
  if(MINGW)
    set(SOCI_COMPILER_NAME "mingw-${GCC_VERSION}")
  endif( MINGW )
  
  if(CMAKE_GENERATOR MATCHES "Win64")
    set(SOCI_PLATFORM_NAME "x64")
  else()
    set(SOCI_PLATFORM_NAME "win32")
  endif()
endif(WIN32)

if(UNIX)
  set(SOCI_COMPILER_NAME "gcc-${GCC_VERSION}")
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    set(SOCI_PLATFORM_NAME "x64")
  else()
    set(SOCI_PLATFORM_NAME "x86")
  endif()
endif(UNIX)

if(NOT SOCI_COMPILER_NAME)
  colormsg(_RED_ "WARNING:")
  colormsg(RED "Could not determine compiler toolset name to set SOCI_COMPILER_NAME variable.")
endif()

if(NOT SOCI_PLATFORM_NAME)
  colormsg(_RED_ "WARNING:")
  colormsg(RED "Could not determine platform name to set SOCI_PLATFORM_NAME variable.")
endif()
