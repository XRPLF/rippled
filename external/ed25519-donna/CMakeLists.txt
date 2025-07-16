cmake_minimum_required(VERSION 3.11)

project(ed25519
  LANGUAGES C
)

if(PROJECT_NAME STREQUAL CMAKE_PROJECT_NAME)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/output/$<CONFIG>/lib")
endif()

if(NOT TARGET OpenSSL::SSL)
  find_package(OpenSSL)
endif()

add_library(ed25519 STATIC
  ed25519.c
)
add_library(ed25519::ed25519 ALIAS ed25519)
target_link_libraries(ed25519 PUBLIC OpenSSL::SSL)

include(GNUInstallDirs)

#[=========================================================[
   NOTE for macos:
   https://github.com/floodyberry/ed25519-donna/issues/29
   our source for ed25519-donna-portable.h has been
   patched to workaround this.
#]=========================================================]
target_include_directories(ed25519 PUBLIC
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

install(
  TARGETS ed25519
  EXPORT ${PROJECT_NAME}-exports
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(
  EXPORT ${PROJECT_NAME}-exports
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
  FILE ${PROJECT_NAME}-targets.cmake
  NAMESPACE ${PROJECT_NAME}::
)
install(
  FILES ed25519.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
