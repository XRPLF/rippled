################################################################################
# SociBackend.cmake - part of CMake configuration of SOCI library
################################################################################
# Copyright (C) 2010-2013 Mateusz Loskot <mateusz@loskot.net>
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

macro(soci_backend_deps_found NAME DEPS SUCCESS)
  #message(STATUS "DEPS=${DEPS}")

  # Determine required dependencies
  set(DEPS_INCLUDE_DIRS)
  set(DEPS_LIBRARIES)
  set(DEPS_NOT_FOUND)

  # CMake 2.8+ syntax only:
  #foreach(dep IN LISTS DEPS)
  foreach(dep ${DEPS})
    soci_check_package_found(${dep} DEPEND_FOUND)
    if(NOT DEPEND_FOUND)
      list(APPEND DEPS_NOT_FOUND ${dep})
    else()
      string(TOUPPER "${dep}" DEPU)
      if( ${DEPU}_INCLUDE_DIR )
        list(APPEND DEPS_INCLUDE_DIRS ${${DEPU}_INCLUDE_DIR})
      endif()
      if( ${DEPU}_INCLUDE_DIRS )
        list(APPEND DEPS_INCLUDE_DIRS ${${DEPU}_INCLUDE_DIRS})
      endif()
      list(APPEND DEPS_LIBRARIES ${${DEPU}_LIBRARIES})
    endif()
  endforeach()

  list(LENGTH DEPS_NOT_FOUND NOT_FOUND_COUNT)

  if (NOT_FOUND_COUNT GREATER 0)
    set(${SUCCESS} False)
  else()
    set(${NAME}_DEPS_INCLUDE_DIRS ${DEPS_INCLUDE_DIRS})
    set(${NAME}_DEPS_LIBRARIES ${DEPS_LIBRARIES})
    set(${SUCCESS} True)
  endif()

  #message(STATUS "soci_backend_deps_found: ${SUCCESS}=${${SUCCESS}}")
endmacro()

# Defines project of a database backend for SOCI library
#
# soci_backend(backendname
#              DEPENDS dependency1 dependency2
#              DESCRIPTION description
#              AUTHORS author1 author2
#              MAINTAINERS maintainer1 maintainer2)
#
macro(soci_backend NAME)
  parse_arguments(THIS_BACKEND
    "DEPENDS;DESCRIPTION;AUTHORS;MAINTAINERS;"
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

  soci_backend_deps_found(${NAMEU} "${THIS_BACKEND_DEPENDS}" ${NAMEU}_DEPS_FOUND)
  if(NOT ${NAMEU}_DEPS_FOUND)

    colormsg(_RED_ "WARNING: Some required dependencies of ${NAME} backend not found:")

    if(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} LESS 2.8)
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

  else()

    if(${THIS_BACKEND_OPTION})

      get_directory_property(THIS_INCLUDE_DIRS INCLUDE_DIRECTORIES)
      get_directory_property(THIS_COMPILE_DEFS COMPILE_DEFINITIONS)

      # Backend-specific depedencies
      set(THIS_BACKEND_DEPENDS_INCLUDE_DIRS ${${NAMEU}_DEPS_INCLUDE_DIRS})
      set(THIS_BACKEND_DEPENDS_LIBRARIES ${${NAMEU}_DEPS_LIBRARIES})
      set(THIS_BACKEND_DEPENDS_DEFS ${${NAMEU}_DEPS_DEFS})

      # Collect include directories
      list(APPEND THIS_INCLUDE_DIRS ${SOCI_SOURCE_DIR}/include/private)
      list(APPEND THIS_INCLUDE_DIRS ${THIS_BACKEND_DEPENDS_INCLUDE_DIRS})
      # Collect compile definitions
      list(APPEND THIS_COMPILE_DEFS ${THIS_BACKEND_DEPENDS_DEFS})

      set_directory_properties(PROPERTIES
        INCLUDE_DIRECTORIES "${THIS_INCLUDE_DIRS}"
        COMPILE_DEFINITIONS "${THIS_COMPILE_DEFS}")

      # Backend target
      set(THIS_BACKEND_VAR SOCI_${NAMEU})
      set(THIS_BACKEND_TARGET ${PROJECTNAMEL}_${NAMEL})
      set(THIS_BACKEND_TARGET_VAR ${THIS_BACKEND_VAR}_TARGET)
      set(${THIS_BACKEND_TARGET_VAR} ${THIS_BACKEND_TARGET})

      soci_target_output_name(${THIS_BACKEND_TARGET} ${THIS_BACKEND_VAR}_OUTPUT_NAME)

      set(THIS_BACKEND_OUTPUT_NAME ${${THIS_BACKEND_VAR}_OUTPUT_NAME})
      set(THIS_BACKEND_OUTPUT_NAME_VAR ${THIS_BACKEND_VAR}_OUTPUT_NAME)

      set(${THIS_BACKEND_VAR}_COMPILE_DEFINITIONS ${THIS_COMPILE_DEFS})
      set(THIS_BACKEND_COMPILE_DEFINITIONS_VAR ${THIS_BACKEND_VAR}_COMPILE_DEFINITIONS)

      set(${THIS_BACKEND_VAR}_INCLUDE_DIRECTORIES ${THIS_INCLUDE_DIRS})
      set(THIS_BACKEND_INCLUDE_DIRECTORIES_VAR ${THIS_BACKEND_VAR}_INCLUDE_DIRECTORIES)

      # Backend installable headers and sources
      file(GLOB THIS_BACKEND_HEADERS ${SOCI_SOURCE_DIR}/include/soci/${NAMEL}/*.h)
      file(GLOB THIS_BACKEND_SOURCES *.cpp)
      set(THIS_BACKEND_HEADERS_VAR SOCI_${NAMEU}_HEADERS)
      set(${THIS_BACKEND_HEADERS_VAR} ${THIS_BACKEND_HEADERS})
      # Group source files for IDE source explorers (e.g. Visual Studio)
      source_group("Header Files" FILES ${THIS_BACKEND_HEADERS})
      source_group("Source Files" FILES ${THIS_BACKEND_SOURCES})
      source_group("CMake Files" FILES CMakeLists.txt)

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
            OUTPUT_NAME ${THIS_BACKEND_OUTPUT_NAME}
            DEFINE_SYMBOL SOCI_DLL)
        else()
          set_target_properties(${THIS_BACKEND_TARGET}
            PROPERTIES
            SOVERSION ${${PROJECT_NAME}_SOVERSION}
            INSTALL_NAME_DIR ${CMAKE_INSTALL_PREFIX}/lib)

          if(APPLE)
            set_target_properties(${THIS_BACKEND_TARGET}
              PROPERTIES
              LINK_FLAGS "-Wl,-flat_namespace -Wl,-undefined -Wl,suppress")
          endif()
        endif()

        set_target_properties(${THIS_BACKEND_TARGET}
          PROPERTIES
          VERSION ${${PROJECT_NAME}_VERSION}
          CLEAN_DIRECT_OUTPUT 1)
      endif()

      # Static library target
      if(SOCI_STATIC)
        set(THIS_BACKEND_TARGET_STATIC ${THIS_BACKEND_TARGET}_static)

        add_library(${THIS_BACKEND_TARGET_STATIC}
          STATIC
          ${THIS_BACKEND_SOURCES}
          ${THIS_BACKEND_HEADERS})

        # Still need to link the libraries for tests to work
        target_link_libraries (${THIS_BACKEND_TARGET_STATIC}
          ${THIS_BACKEND_DEPENDS_LIBRARIES}
        )

        set_target_properties(${THIS_BACKEND_TARGET_STATIC}
          PROPERTIES
          OUTPUT_NAME ${THIS_BACKEND_OUTPUT_NAME}
          PREFIX "lib"
          CLEAN_DIRECT_OUTPUT 1)
      endif()

      # Backend installation
      install(FILES ${THIS_BACKEND_HEADERS}
        DESTINATION
        ${INCLUDEDIR}/${PROJECTNAMEL}/${NAMEL})

      if (SOCI_SHARED)
        install(TARGETS ${THIS_BACKEND_TARGET}
          EXPORT SOCI
          RUNTIME DESTINATION ${BINDIR}
          LIBRARY DESTINATION ${LIBDIR}
          ARCHIVE DESTINATION ${LIBDIR})
      endif()

      if (SOCI_STATIC)
        install(TARGETS ${THIS_BACKEND_TARGET_STATIC}
          EXPORT SOCI
          RUNTIME DESTINATION ${BINDIR}
          LIBRARY DESTINATION ${LIBDIR}
          ARCHIVE DESTINATION ${LIBDIR}
         )
      endif()

      install(EXPORT SOCI NAMESPACE SOCI:: DESTINATION cmake)

    else()
        colormsg(HIRED "${NAME}" RED "backend disabled, since")
    endif()

  endif()

  boost_report_value(${THIS_BACKEND_OPTION})

  if(${THIS_BACKEND_OPTION})
    boost_report_value(${THIS_BACKEND_TARGET_VAR})
    boost_report_value(${THIS_BACKEND_OUTPUT_NAME_VAR})
    boost_report_value(${THIS_BACKEND_COMPILE_DEFINITIONS_VAR})
    boost_report_value(${THIS_BACKEND_INCLUDE_DIRECTORIES_VAR})
  endif()

  # LOG
  #message("soci_backend:")
  #message("NAME: ${NAME}")
  #message("${THIS_BACKEND_OPTION} = ${SOCI_BACKEND_SQLITE3}")
  #message("DEPENDS: ${THIS_BACKEND_DEPENDS}")
  #message("DESCRIPTION: ${THIS_BACKEND_DESCRIPTION}")
  #message("AUTHORS: ${THIS_BACKEND_AUTHORS}")
  #message("MAINTAINERS: ${THIS_BACKEND_MAINTAINERS}")
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
#	  CONNSTR "my test connection"
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
    string(TOLOWER "${TEST_FULL_NAME}" TEST_TARGET)
    string(TOUPPER "${TEST_FULL_NAME}" NAMEU)

    soci_backend_deps_found(${NAMEU} "${THIS_TEST_DEPENDS}" ${NAMEU}_DEPS_FOUND)
    if(${NAMEU}_DEPS_FOUND)
      get_directory_property(THIS_INCLUDE_DIRS INCLUDE_DIRECTORIES)
      get_directory_property(THIS_COMPILE_DEFS COMPILE_DEFINITIONS)

      set(THIS_TEST_DEPENDS_INCLUDE_DIRS ${${NAMEU}_DEPS_INCLUDE_DIRS})
      set(THIS_TEST_DEPENDS_LIBRARIES ${${NAMEU}_DEPS_LIBRARIES})
      set(THIS_TEST_DEPENDS_DEFS ${${NAMEU}_DEPS_DEFS})

      list(APPEND THIS_INCLUDE_DIRS ${THIS_TEST_DEPENDS_INCLUDE_DIRS})
      list(APPEND THIS_COMPILE_DEFS ${THIS_TEST_DEPENDS_DEFS})

      set_directory_properties(PROPERTIES
        INCLUDE_DIRECTORIES "${THIS_INCLUDE_DIRS}"
        COMPILE_DEFINITIONS "${THIS_COMPILE_DEFS}")
    else()
       colormsg(_RED_ "WARNING: Some dependencies of ${THIS_TEST_BACKEND} test not found")
    endif()

    set(TEST_CONNSTR_VAR ${TEST_FULL_NAME}_CONNSTR)
    set(${TEST_CONNSTR_VAR} ""
      CACHE STRING "Connection string for ${BACKENDU} test")

    if(NOT ${TEST_CONNSTR_VAR} AND THIS_TEST_CONNSTR)
      set(${TEST_CONNSTR_VAR} ${THIS_TEST_CONNSTR})
      if(${TEST_CONNSTR_VAR} MATCHES ".dsn")
        set(_dsnpath "${CMAKE_CURRENT_SOURCE_DIR}/${${TEST_CONNSTR_VAR}}")
        set(${TEST_CONNSTR_VAR} "FILEDSN=${_dsnpath}")
      endif()
    endif()

    boost_message_value(${TEST_CONNSTR_VAR})

    if( SOCI_SHARED )
      # Shared libraries test
      add_executable(${TEST_TARGET} ${THIS_TEST_SOURCE})

      target_link_libraries(${TEST_TARGET}
        ${SOCI_CORE_DEPS_LIBS}
        ${THIS_TEST_DEPENDS_LIBRARIES}
        soci_core
        soci_${BACKENDL})

      add_test(${TEST_TARGET}
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_TARGET}
        ${${TEST_CONNSTR_VAR}})

      soci_backend_test_create_vcxproj_user(${TEST_TARGET} "\"${${TEST_CONNSTR_VAR}}\"")

      # Ask make check to try to build tests first before executing them
      add_dependencies(check ${TEST_TARGET})
    endif(SOCI_SHARED)

    # Static libraries test
    if(SOCI_STATIC)
      set(TEST_TARGET_STATIC ${TEST_TARGET}_static)

      add_executable(${TEST_TARGET_STATIC} ${THIS_TEST_SOURCE})

      target_link_libraries(${TEST_TARGET_STATIC}
        ${SOCI_CORE_DEPS_LIBS}
        ${THIS_TEST_DEPENDS_LIBRARIES}
        soci_${BACKENDL}_static
        soci_core_static)

      add_test(${TEST_TARGET_STATIC}
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_TARGET_STATIC}
        ${${TEST_CONNSTR_VAR}})

      soci_backend_test_create_vcxproj_user(${TEST_TARGET_STATIC} "\"${${TEST_CONNSTR_VAR}}\"")

      # Ask make check to try to build tests first before executing them
      add_dependencies(check ${TEST_TARGET_STATIC})
    endif(SOCI_STATIC)



    # Group source files for IDE source explorers (e.g. Visual Studio)
    source_group("Source Files" FILES ${THIS_TEST_SOURCE})
    source_group("CMake Files" FILES CMakeLists.txt)

  endif()
endmacro()
