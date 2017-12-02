###############################################################################
#
# This file is part of CMake configuration for SOCI library
#
# Copyright (C) 2010 Mateusz Loskot
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
#
###############################################################################
colormsg(_HIBLUE_ "Configuring SOCI backend libraries:")

# First, we'll investigate what can be found from database engines
foreach(dep ${SOCI_BACKENDS_DB_DEPENDENCIES})
	string(TOUPPER ${dep} depUP)
	if (WITH_${depUP})
		find_package(${dep})
	endif()
	if(${dep}_FOUND OR ${depUP}_FOUND)
		set(${depUP}_FOUND ON)
	else()
		set(${depUP}_FOUND OFF)
	endif()
	set(SOCI_HAVE_${depUP} OFF CACHE INTERNAL "${depUP} backend")
endforeach()

# get all files in backends
file(GLOB backend_dirs RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)

# empty backend always on by default
option(SOCI_EMPTY "Build empty backend" ON)
if(SOCI_EMPTY)
	set(WITH_EMPTY ON)
	set(EMPTY_FOUND ON)
endif()

# enable only found backends
foreach(dir ${backend_dirs})
	if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${dir})
		if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt)
			string(TOUPPER ${dir} dirUP)
			if(${dirUP}_FOUND AND WITH_${dirUP})
				add_subdirectory(${dir})
				set(SOCI_HAVE_${dirUP} ON CACHE INTERNAL "${dirUP} backend")
			endif()
		endif()
	endif()
endforeach()

message(STATUS "")
