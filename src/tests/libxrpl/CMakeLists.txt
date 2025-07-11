include(xrpl_add_test)

# Test requirements.
find_package(doctest REQUIRED)

# After conan setup or before any other targets are defined
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/wasm_module.wasm
  COMMAND cargo build --release --target wasm32-unknown-unknown
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/test/wasm_fixtures
  COMMENT "Building Rust WASM module"
)

# Step 1: Define where the wasm output lives
set(RUST_WASM_OUTPUT
    ${CMAKE_SOURCE_DIR}/src/test/wasm_fixtures/target/wasm32-unknown-unknown/release/kyc.wasm)

# Step 2: Tell CMake to run cargo build
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/kyc.wasm
    COMMAND cargo build --release --target wasm32-unknown-unknown
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/test/wasm_fixtures
    COMMENT "Building Rust WASM module"
)

# Step 3: Define a target for it
add_custom_target(kyc_wasm ALL
    DEPENDS ${CMAKE_BINARY_DIR}/kyc.wasm
)

# Step 4: Copy the wasm file to your build dir
add_custom_command(
    TARGET kyc_wasm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${RUST_WASM_OUTPUT}
            ${CMAKE_BINARY_DIR}/kyc.wasm
    COMMENT "Copying .wasm to build directory"
)

set(EMBED_HEADER ${CMAKE_BINARY_DIR}/wasm_test_fixtures.h)

add_custom_command(
  OUTPUT ${EMBED_HEADER}
  COMMAND xxd -i -n wasm_testcase_a ${CMAKE_BINARY_DIR}/kyc.wasm > ${EMBED_HEADER}
#   COMMAND xxd -i -n wasm_testcase_b ${WASM_B} >> ${EMBED_HEADER}
  COMMENT "Embedding multiple WASM testcases into a single header"
)

add_custom_target(embed_wasm_modules ALL
  DEPENDS ${EMBED_HEADER}
)

add_custom_target(embed_wasm_header ALL
  DEPENDS ${CMAKE_BINARY_DIR}/wasm_test_fixtures.h
)

add_dependencies(rippled embed_wasm_header)
target_include_directories(rippled PRIVATE ${CMAKE_BINARY_DIR})

# Common library dependencies for the rest of the tests.
add_library(xrpl.imports.test INTERFACE)
target_link_libraries(xrpl.imports.test INTERFACE doctest::doctest xrpl.libxrpl)

# One test for each module.
xrpl_add_test(basics)
target_link_libraries(xrpl.test.basics PRIVATE xrpl.imports.test)
xrpl_add_test(crypto)
target_link_libraries(xrpl.test.crypto PRIVATE xrpl.imports.test)
