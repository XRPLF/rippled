################################################################################
# SociBackend.cmake - part of CMake configuration of SOCI library
################################################################################
# Copyright (C) 2010 Mateusz Loskot <mateusz@loskot.net>
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
################################################################################
# Macros in this module:
#   
#   soci_backend
#     - defines project of a database backend for SOCI library
#
#   soci_backend_test
#     - defines test project of a database backend for SOCI library
################################################################################

# Defines project of a database backend for SOCI library
#
# soci_backend(backendname
#              HEADERS header1 header2
#              DEPENDS dependency1 dependency2
#              DESCRIPTION description
#              AUTHORS author1 author2
#              MAINTAINERS maintainer1 maintainer2)
#
macro(soci_backend NAME)
  parse_arguments(THIS_BACKEND
    "HEADERS;DEPENDS;DESCRIPTION;AUTHORS;MAINTAINERS;"
    ""
    ${ARGN})

  colormsg(HIGREEN "${NAME} - ${THIS_BACKEND_DESCRIPTION}")

  # Backend name variants utils
  string(TOLOWER "${PROJECT_NAME}" PROJECTNAMEL)
  string(TOLOWER "${NAME}" NAMEL)
  string(TOUPPER "${NAME}" NAMEU)

  # Backend option available to user
  set(THIS_BACKEND_OPTION SOCI_${NAMEU})
  option(${THIS_BACKEND_OPTION}
    "Attempt to build ${PROJECT_NAME} backend for ${NAME}" ON)

  # Determine required dependencies
  set(THIS_BACKEND_DEPENDS_INCLUDE_DIRS)
  set(THIS_BACKEND_DEPENDS_LIBRARIES)
  set(THIS_BACKEND_DEPENDS_DEFS)
  set(DEPENDS_NOT_FOUND)

  # CMake 2.8+ syntax only:
  #foreach(dep IN LISTS THIS_BACKEND_DEPENDS)
  foreach(dep ${THIS_BACKEND_DEPENDS})

    soci_check_package_found(${dep} DEPEND_FOUND)
    if(NOT DEPEND_FOUND)
      list(APPEND DEPENDS_NOT_FOUND ${dep}) 
    else()
      string(TOUPPER "${dep}" DEPU)
      list(APPEND THIS_BACKEND_DEPENDS_INCLUDE_DIRS ${${DEPU}_INCLUDE_DIR})
      list(APPEND THIS_BACKEND_DEPENDS_INCLUDE_DIRS ${${DEPU}_INCLUDE_DIRS})
      list(APPEND THIS_BACKEND_DEPENDS_LIBRARIES ${${DEPU}_LIBRARIES})
      list(APPEND THIS_BACKEND_DEPENDS_DEFS -DHAVE_${DEPU}=1)
    endif()
  endforeach()

  list(LENGTH DEPENDS_NOT_FOUND NOT_FOUND_COUNT)

  if (NOT_FOUND_COUNT GREATER 0)

    colormsg(_RED_ "WARNING:")
    colormsg(RED "Some required dependencies of ${NAME} backend not found:")

    if (${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} LESS 2.8)
      foreach(dep ${DEPENDS_NOT_FOUND})
        colormsg(RED "   ${dep}")
      endforeach()
    else()
      foreach(dep IN LISTS DEPENDS_NOT_FOUND)
        colormsg(RED "   ${dep}")
      endforeach()
    endif()

    # TODO: Abort or warn compilation may fail? --mloskot
    colormsg(RED "Skipping")

    set(${THIS_BACKEND_OPTION} OFF)

  else(NOT_FOUND_COUNT GREATER 0)

    if(${THIS_BACKEND_OPTION})

      # Backend-specific include directories
      list(APPEND THIS_BACKEND_DEPENDS_INCLUDE_DIRS ${SOCI_SOURCE_DIR}/core)
      set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES
		"${THIS_BACKEND_DEPENDS_INCLUDE_DIRS}")

      # Backend-specific preprocessor definitions
      add_definitions(${THIS_BACKEND_DEPENDS_DEFS})

      # Backend installable headers and sources
      if (NOT THIS_BACKEND_HEADERS)
		file(GLOB THIS_BACKEND_HEADERS *.h)
      endif()
      file(GLOB THIS_BACKEND_SOURCES *.cpp)
      set(THIS_BACKEND_HEADERS_VAR SOCI_${NAMEU}_HEADERS)
      set(${THIS_BACKEND_HEADERS_VAR} ${THIS_BACKEND_HEADERS}) 

	  # Group source files for IDE source explorers (e.g. Visual Studio)
      source_group("Header Files" FILES ${THIS_BACKEND_HEADERS})
	  source_group("Source Files" FILES ${THIS_BACKEND_SOURCES})
      source_group("CMake Files" FILES CMakeLists.txt)

      # Backend target
      set(THIS_BACKEND_TARGET ${PROJECTNAMEL}_${NAMEL})
      set(THIS_BACKEND_TARGET_VAR SOCI_${NAMEU}_TARGET)
      set(${THIS_BACKEND_TARGET_VAR} ${THIS_BACKEND_TARGET})
      
      soci_target_output_name(${THIS_BACKEND_TARGET} ${THIS_BACKEND_TARGET_VAR}_OUTPUT_NAME)

      set(THIS_BACKEND_TARGET_OUTPUT_NAME ${${THIS_BACKEND_TARGET_VAR}_OUTPUT_NAME})
      set(THIS_BACKEND_TARGET_OUTPUT_NAME_VAR ${THIS_BACKEND_TARGET_VAR}_OUTPUT_NAME)

      # TODO: Extract as macros: soci_shared_lib_target and soci_static_lib_target --mloskot

      # Shared library target
      if (SOCI_SHARED)
        add_library(${THIS_BACKEND_TARGET}
          SHARED
          ${THIS_BACKEND_SOURCES}
          ${THIS_BACKEND_HEADERS})

        target_link_libraries(${THIS_BACKEND_TARGET}
	  ${SOCI_CORE_TARGET}
	  ${THIS_BACKEND_DEPENDS_LIBRARIES})

        if(WIN32)
	  set_target_properties(${THIS_BACKEND_TARGET}
            PROPERTIES
            OUTPUT_NAME ${THIS_BACKEND_TARGET_OUTPUT_NAME}
            DEFINE_SYMBOL SOCI_DLL)
        else()
	  set_target_properties(${THIS_BACKEND_TARGET}
            PROPERTIES
            SOVERSION ${${PROJECT_NAME}_SOVERSION}
            INSTALL_NAME_DIR ${CMAKE_INSTALL_PREFIX}/lib)
        endif()

        set_target_properties(${THIS_BACKEND_TARGET}
          PROPERTIES
          VERSION ${${PROJECT_NAME}_VERSION}
          CLEAN_DIRECT_OUTPUT 1)
      endif()

      # Static library target
      if (SOCI_STATIC)
        set(THIS_BACKEND_TARGET_STATIC ${THIS_BACKEND_TARGET}_static)

        add_library(${THIS_BACKEND_TARGET_STATIC}
          STATIC
          ${THIS_BACKEND_SOURCES}
          ${THIS_BACKEND_HEADERS})

        set_target_properties(${THIS_BACKEND_TARGET_STATIC}
		      PROPERTIES
		      OUTPUT_NAME ${THIS_BACKEND_TARGET_OUTPUT_NAME}
		      PREFIX "lib"
		      CLEAN_DIRECT_OUTPUT 1)
      endif()

      # Backend installation
      install(FILES ${THIS_BACKEND_HEADERS}
          DESTINATION
          ${INCLUDEDIR}/${PROJECTNAMEL}/${NAMEL})

      if (SOCI_SHARED)
        install(TARGETS ${THIS_BACKEND_TARGET}
	  RUNTIME DESTINATION ${BINDIR}
	  LIBRARY DESTINATION ${LIBDIR}
	  ARCHIVE DESTINATION ${LIBDIR})
      endif()

      if (SOCI_SHARED)
        install(TARGETS ${THIS_BACKEND_TARGET_STATIC}
	  RUNTIME DESTINATION ${BINDIR}
	  LIBRARY DESTINATION ${LIBDIR}
	  ARCHIVE DESTINATION ${LIBDIR})
      endif()

	else()
      colormsg(HIRED "${NAME}" RED "backend disabled, since")
	endif()

  endif(NOT_FOUND_COUNT GREATER 0)

  boost_report_value(${THIS_BACKEND_OPTION})

  if(${THIS_BACKEND_OPTION})
    boost_report_value(${THIS_BACKEND_TARGET_VAR})
    boost_report_value(${THIS_BACKEND_TARGET_OUTPUT_NAME_VAR})
    boost_report_value(${THIS_BACKEND_HEADERS_VAR})

    soci_report_directory_property(COMPILE_DEFINITIONS)    
  endif()

  # LOG
  #message("soci_backend:")
  #message("NAME: ${NAME}")
  #message("${THIS_BACKEND_OPTION} = ${SOCI_BACKEND_SQLITE3}")
  #message("DEPENDS: ${THIS_BACKEND_DEPENDS}")
  #message("DESCRIPTION: ${THIS_BACKEND_DESCRIPTION}")
  #message("AUTHORS: ${THIS_BACKEND_AUTHORS}")
  #message("MAINTAINERS: ${THIS_BACKEND_MAINTAINERS}")
  #message("HEADERS: ${THIS_BACKEND_HEADERS}")
  #message("SOURCES: ${THIS_BACKEND_SOURCES}")
  #message("DEPENDS_LIBRARIES: ${THIS_BACKEND_DEPENDS_LIBRARIES}")
  #message("DEPENDS_INCLUDE_DIRS: ${THIS_BACKEND_DEPENDS_INCLUDE_DIRS}")
endmacro()

# Generates .vcxproj.user for target of each test.
#
# soci_backend_test_create_vcxproj_user(
#    PostgreSQLTest
#    "host=localhost dbname=soci_test user=mloskot")
#
function(soci_backend_test_create_vcxproj_user TARGET_NAME TEST_CMD_ARGS)
  if(MSVC)
    set(SYSTEM_NAME $ENV{USERDOMAIN})
    set(USER_NAME $ENV{USERNAME})
    set(SOCI_TEST_CMD_ARGS ${TEST_CMD_ARGS})

    if(MSVC_VERSION EQUAL 1600)
      configure_file(
        ${SOCI_SOURCE_DIR}/cmake/resources/vs2010-test-cmd-args.vcxproj.user.in
        ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.vcxproj.user
        @ONLY)
    endif()
  endif()
endfunction(soci_backend_test_create_vcxproj_user)

# Defines test project of a database backend for SOCI library
#
# soci_backend_test(BACKEND mybackend SOURCE mytest1.cpp
#   NAME mytest1
#	CONNSTR "my test connection"
#   DEPENDS library1 library2)
#
macro(soci_backend_test)
  parse_arguments(THIS_TEST
    "BACKEND;SOURCE;CONNSTR;NAME;DEPENDS;"
    ""
    ${ARGN})

  # Test backend name
  string(TOUPPER "${THIS_TEST_BACKEND}" BACKENDU)
  string(TOLOWER "${THIS_TEST_BACKEND}" BACKENDL)

  if(SOCI_TESTS AND SOCI_${BACKENDU} AND NOT SOCI_${BACKENDU}_DO_NOT_TEST)

    # Test name
    if(THIS_TEST_NAME)
	  string(TOUPPER "${THIS_TEST_NAME}" NAMEU)
	  set(TEST_FULL_NAME SOCI_${BACKENDU}_TEST_${NAMEU})
	else()
	  set(TEST_FULL_NAME SOCI_${BACKENDU}_TEST)
    endif()

    set(TEST_CONNSTR_VAR ${TEST_FULL_NAME}_CONNSTR)
    set(${TEST_CONNSTR_VAR} ""
      CACHE STRING "Connection string for ${BACKENDU} test")
    
    if(NOT ${TEST_CONNSTR_VAR} AND THIS_TEST_CONNSTR)
      set(${TEST_CONNSTR_VAR} ${THIS_TEST_CONNSTR})
    endif()
    boost_report_value(${TEST_CONNSTR_VAR})

    include_directories(${SOCI_SOURCE_DIR}/core/test)
    include_directories(${SOCI_SOURCE_DIR}/backends/${BACKENDL})

    # TODO: Find more generic way of adding Boost to core and backend tests only.
    #       Ideally, from within Boost.cmake.
	set(SOCI_TEST_DEPENDENCIES)
    if(Boost_FOUND)
	  include_directories(${Boost_INCLUDE_DIRS})
	  if(Boost_DATE_TIME_FOUND)
		set(SOCI_TEST_DEPENDENCIES ${Boost_DATE_TIME_LIBRARY})
		add_definitions(-DHAVE_BOOST_DATE_TIME=1)
	  endif()
	endif()

    string(TOLOWER "${TEST_FULL_NAME}" TEST_TARGET)

	set(TEST_HEADERS ${PROJECT_SOURCE_DIR}/core/test/common-tests.h)

    # Shared libraries test
    add_executable(${TEST_TARGET} ${TEST_HEADERS} ${THIS_TEST_SOURCE})

    target_link_libraries(${TEST_TARGET}
      ${SOCI_CORE_TARGET}
      ${SOCI_${BACKENDU}_TARGET}
      ${${BACKENDU}_LIBRARIES}
	  ${SOCI_TEST_DEPENDENCIES})

    add_test(${TEST_TARGET}
      ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_TARGET}
      ${${TEST_CONNSTR_VAR}})

    soci_backend_test_create_vcxproj_user(${TEST_TARGET} "\"${${TEST_CONNSTR_VAR}}\"")

    # Static libraries test
    if(SOCI_STATIC)
      set(TEST_TARGET_STATIC ${TEST_TARGET}_static)

      add_executable(${TEST_TARGET_STATIC} ${TEST_HEADERS} ${THIS_TEST_SOURCE})

      target_link_libraries(${TEST_TARGET_STATIC}
        ${SOCI_CORE_TARGET_STATIC}
        ${SOCI_${BACKENDU}_TARGET}_static
        ${${BACKENDU}_LIBRARIES}
        ${SOCI_CORE_STATIC_DEPENDENCIES}
        ${SOCI_TEST_DEPENDENCIES})

      add_test(${TEST_TARGET_STATIC}
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_TARGET_STATIC}
        ${${TEST_CONNSTR_VAR}})
    
      soci_backend_test_create_vcxproj_user(${TEST_TARGET_STATIC} "\"${${TEST_CONNSTR_VAR}}\"")
    endif(SOCI_STATIC)

    # Ask make check to try to build tests first before executing them
    add_dependencies(check ${TEST_TARGET} ${TEST_TARGET_STATIC})

    # Group source files for IDE source explorers (e.g. Visual Studio)
    source_group("Header Files" FILES ${TEST_HEADERS})
    source_group("Source Files" FILES ${THIS_TEST_SOURCE})
    source_group("CMake Files" FILES CMakeLists.txt)

  endif()

  # LOG
  #message("NAME=${NAME}")
  #message("THIS_TEST_NAME=${THIS_TEST_NAME}")
  #message("THIS_TEST_BACKEND=${THIS_TEST_BACKEND}")
  #message("THIS_TEST_CONNSTR=${THIS_TEST_CONNSTR}")
  #message("THIS_TEST_SOURCE=${THIS_TEST_SOURCE}")
  #message("THIS_TEST_OPTION=${THIS_TEST_OPTION}")

endmacro()
