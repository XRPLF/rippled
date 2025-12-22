include(FetchContent)

ExternalProject_Add(
  dilithium_src
  PREFIX ${nih_cache_path}
  GIT_REPOSITORY https://github.com/Transia-RnD/dilithium.git
  GIT_TAG master
  CONFIGURE_COMMAND ""
  LOG_BUILD ON
  BUILD_IN_SOURCE 0
  BUILD_COMMAND
    COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/ref <BINARY_DIR>/ref
    COMMAND make -C <BINARY_DIR>/ref clean
    COMMAND /bin/sh -c "CFLAGS='-DDILITHIUM_MODE=2 -DDILITHIUM_RANDOMIZED_SIGNING' make -C <BINARY_DIR>/ref libdilithium2_ref.a libfips202_ref.a"
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS
      <BINARY_DIR>/ref/libdilithium2_ref.a
      <BINARY_DIR>/ref/libfips202_ref.a
)

ExternalProject_Get_Property(dilithium_src SOURCE_DIR BINARY_DIR)
set(dilithium_src_SOURCE_DIR "${SOURCE_DIR}")
set(dilithium_src_BINARY_DIR "${BINARY_DIR}")

# Include the reference implementation headers from source
include_directories("${dilithium_src_SOURCE_DIR}/ref")

# Create imported targets for each static library using BINARY_DIR
add_library(dilithium::dilithium2_ref STATIC IMPORTED GLOBAL)
set_target_properties(dilithium::dilithium2_ref PROPERTIES
  IMPORTED_LOCATION "${dilithium_src_BINARY_DIR}/ref/libdilithium2_ref.a"
  INTERFACE_INCLUDE_DIRECTORIES "${dilithium_src_SOURCE_DIR}/ref/"
)

add_library(dilithium::libfips202_ref STATIC IMPORTED GLOBAL)
set_target_properties(dilithium::libfips202_ref PROPERTIES
  IMPORTED_LOCATION "${dilithium_src_BINARY_DIR}/ref/libfips202_ref.a"
  INTERFACE_INCLUDE_DIRECTORIES "${dilithium_src_SOURCE_DIR}/ref/"
)

# Add dependencies to ensure the external project is built first
add_dependencies(dilithium::dilithium2_ref dilithium_src)
add_dependencies(dilithium::libfips202_ref dilithium_src)

# Add a custom command to generate randombytes.c
add_custom_command(
  OUTPUT "${dilithium_src_BINARY_DIR}/ref/randombytes.c"
  COMMAND ${CMAKE_COMMAND} -E copy "${dilithium_src_SOURCE_DIR}/ref/randombytes.c" "${dilithium_src_BINARY_DIR}/ref/randombytes.c"
  DEPENDS dilithium_src
)

# Add the randombytes_ref library
add_library(randombytes_ref STATIC "${dilithium_src_BINARY_DIR}/ref/randombytes.c")

# Set language properties
set_target_properties(randombytes_ref PROPERTIES
  LINKER_LANGUAGE C
  C_STANDARD 99
)

# Suppress the warning about C++-specific flags being passed to C compiler
# The flag -Wno-subobject-linkage is a C++ flag that causes a warning when compiling C code
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  set_source_files_properties("${dilithium_src_BINARY_DIR}/ref/randombytes.c" PROPERTIES
    COMPILE_FLAGS "-Wno-error"
  )
endif()

# Include the Dilithium ref directory for headers
target_include_directories(randombytes_ref PRIVATE "${dilithium_src_SOURCE_DIR}/ref")

# Ensure that randombytes_ref depends on the Dilithium source so it's built after
add_dependencies(randombytes_ref dilithium_src)

# Create an interface library that links to the Dilithium libraries
# Note: Link order matters - libraries that provide symbols must come AFTER libraries that use them
target_link_libraries(xrpl_libs INTERFACE
  dilithium::dilithium2_ref
  dilithium::libfips202_ref
  randombytes_ref
)

# Create alias for convenience
add_library(NIH::dilithium2_ref ALIAS dilithium::dilithium2_ref)