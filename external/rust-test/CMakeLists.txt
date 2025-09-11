cmake_minimum_required(VERSION 3.15)
project(RustTest)

set(CMAKE_INSTALL_CMAKEDIR "lib/cmake/RustTest")

execute_process(COMMAND cargo run --bin install -- --release WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

set(INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/target/install/release/include/")

add_library(RustTest STATIC IMPORTED GLOBAL)

set_target_properties(RustTest PROPERTIES
	IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/target/install/release/lib/RustTest.a"
	INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/target/install/release/include/"
)
