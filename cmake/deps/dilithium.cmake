include(ExternalProject)

ExternalProject_Add(
  dilithium_src
  # Pin to an explicit commit, not a moving branch ref. Bumping this SHA
  # is a supply-chain decision that must be reviewed; never revert to a
  # branch tag here. 3032292 is pq-crystals/dilithium@444cdcc84eb36b66fe27b3a2529ee48f6d8150c2
  # plus one commit that touches only ref/Makefile, adding the
  # libdilithium2_ref.a and libfips202_ref.a targets; every other file
  # under ref/ is byte-identical to upstream.
  #   https://github.com/Transia-RnD/dilithium/commit/3032292cfd4d94e0df9bd49a0098669ca9166aa1
  GIT_REPOSITORY https://github.com/Transia-RnD/dilithium.git
  GIT_TAG 3032292cfd4d94e0df9bd49a0098669ca9166aa1
  GIT_SHALLOW FALSE
  CONFIGURE_COMMAND ""
  LOG_BUILD ON
  BUILD_IN_SOURCE 0
  # No shell: the flags reach make through the environment. -fstack-protector
  # and -fPIC are what XrplCompiler.cmake gives every other translation unit.
  BUILD_COMMAND
    COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/ref <BINARY_DIR>/ref
    COMMAND make -C <BINARY_DIR>/ref clean
    COMMAND ${CMAKE_COMMAND} -E env "CFLAGS=${CMAKE_C_FLAGS} -fstack-protector -fPIC -DDILITHIUM_MODE=2 -DDILITHIUM_RANDOMIZED_SIGNING" make -C <BINARY_DIR>/ref libdilithium2_ref.a libfips202_ref.a
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
# The archive above is compiled with these defines via BUILD_COMMAND's CFLAGS;
# propagate them to every consumer so params.h resolves CRYPTO_PUBLICKEYBYTES /
# CRYPTO_SECRETKEYBYTES to the archive's sizes wherever it is included.
target_compile_definitions(dilithium::dilithium2_ref INTERFACE
  DILITHIUM_MODE=2
  DILITHIUM_RANDOMIZED_SIGNING
)

add_library(dilithium::libfips202_ref STATIC IMPORTED GLOBAL)
set_target_properties(dilithium::libfips202_ref PROPERTIES
  IMPORTED_LOCATION "${dilithium_src_BINARY_DIR}/ref/libfips202_ref.a"
  INTERFACE_INCLUDE_DIRECTORIES "${dilithium_src_SOURCE_DIR}/ref/"
)

# Add dependencies to ensure the external project is built first
add_dependencies(dilithium::dilithium2_ref dilithium_src)
add_dependencies(dilithium::libfips202_ref dilithium_src)

# The pinned ref/Makefile archives $(SOURCES:.c=.o) symmetric-shake.o fips202.o
# into libdilithium2_ref.a, and SOURCES has no randombytes.c, so the archive
# carries no randombytes definition. The only one at link time is the
# extern "C" randombytes in src/libxrpl/protocol/SecretKey.cpp, which draws
# from xrpl::cryptoPrng() rather than reading /dev/urandom directly.

# Create an interface library that links to the Dilithium libraries
# Note: Link order matters - libraries that provide symbols must come AFTER libraries that use them
target_link_libraries(xrpl_libs INTERFACE
  dilithium::dilithium2_ref
  dilithium::libfips202_ref
)

# Create alias for convenience
add_library(NIH::dilithium2_ref ALIAS dilithium::dilithium2_ref)
