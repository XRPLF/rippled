include(FetchContent)

ExternalProject_Add(
  dilithium_src
  PREFIX ${nih_cache_path}
  # Pin to an explicit commit, not a moving branch ref. Bumping this SHA
  # is a supply-chain decision that must be reviewed; never revert to a
  # branch tag here. Upstream:
  #   https://github.com/Transia-RnD/dilithium/commit/3032292cfd4d94e0df9bd49a0098669ca9166aa1
  GIT_REPOSITORY https://github.com/Transia-RnD/dilithium.git
  GIT_TAG 3032292cfd4d94e0df9bd49a0098669ca9166aa1
  GIT_SHALLOW FALSE
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

# Note: We do NOT link the Dilithium library's randombytes.c because we provide
# our own thread-safe implementation in src/libxrpl/protocol/SecretKey.cpp
# that uses xrpld's crypto_prng() instead of direct /dev/urandom access.

# Create an interface library that links to the Dilithium libraries
# Note: Link order matters - libraries that provide symbols must come AFTER libraries that use them
target_link_libraries(xrpl_libs INTERFACE
  dilithium::dilithium2_ref
  dilithium::libfips202_ref
)

# Create alias for convenience
add_library(NIH::dilithium2_ref ALIAS dilithium::dilithium2_ref)
