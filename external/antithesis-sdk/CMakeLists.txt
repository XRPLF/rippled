cmake_minimum_required(VERSION 3.25)
project(antithesis-sdk-cpp VERSION 0.3.1 LANGUAGES CXX)

add_library(antithesis-sdk-cpp INTERFACE antithesis_sdk.h)

target_include_directories(antithesis-sdk-cpp INTERFACE
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

install(
  FILES antithesis_sdk.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
