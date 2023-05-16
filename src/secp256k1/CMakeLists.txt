cmake_minimum_required(VERSION 3.11)

project(secp256k1
  LANGUAGES C
)

if(PROJECT_NAME STREQUAL CMAKE_PROJECT_NAME)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/output/$<CONFIG>/lib")
endif()

add_library(secp256k1 STATIC
  src/secp256k1.c
)
add_library(secp256k1::secp256k1 ALIAS secp256k1)

include(GNUInstallDirs)

target_compile_definitions(secp256k1 PRIVATE
  USE_NUM_NONE
  USE_FIELD_10X26
  USE_FIELD_INV_BUILTIN
  USE_SCALAR_8X32
  USE_SCALAR_INV_BUILTIN
)
target_include_directories(secp256k1
  PUBLIC $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_compile_options(secp256k1 PRIVATE
  $<$<C_COMPILER_ID:MSVC>:-wd4319>
  $<$<NOT:$<C_COMPILER_ID:MSVC>>:
  -Wno-deprecated-declarations
  -Wno-unused-function
  >
  $<$<C_COMPILER_ID:GNU>:-Wno-nonnull-compare>
)

install(
  TARGETS secp256k1
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
  FILES include/secp256k1.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
