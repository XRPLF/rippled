cmake_minimum_required(VERSION 3.21)

set(name example)
set(version 0.1.0)

project(
  ${name}
  VERSION ${version}
  LANGUAGES CXX
)

find_package(xrpl REQUIRED)

add_executable(example)
target_sources(example PRIVATE src/example.cpp)
target_link_libraries(example PRIVATE xrpl::libxrpl)
