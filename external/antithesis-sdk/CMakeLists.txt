cmake_minimum_required(VERSION 3.25)

# Note, version set explicitly by rippled project
project(antithesis-sdk-cpp VERSION 0.4.2 LANGUAGES CXX)

add_library(antithesis-sdk-cpp INTERFACE antithesis_sdk.h)

# Note, both sections below created by rippled project
target_include_directories(antithesis-sdk-cpp INTERFACE
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

install(
  FILES antithesis_sdk.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
