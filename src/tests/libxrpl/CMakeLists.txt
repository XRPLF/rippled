include(xrpl_add_test)

# Test requirements.
find_package(doctest REQUIRED)

file(GLOB_RECURSE WASM_SOURCE_FILES
  ${CMAKE_SOURCE_DIR}/src/test/wasm_fixtures/Cargo.toml
  ${CMAKE_SOURCE_DIR}/src/test/wasm_fixtures/**/*.rs
  ${CMAKE_SOURCE_DIR}/src/test/wasm_fixtures/**/Cargo.toml
  ${CMAKE_SOURCE_DIR}/src/test/wasm_fixtures/**/Cargo.lock
)

# Build Rust WASM fixtures and pass them into wasm_test_fixtures.h
set(RUST_WASM_OUTPUT_DIR
    ${CMAKE_SOURCE_DIR}/src/test/wasm_fixtures/target/wasm32-unknown-unknown/release)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/wasm
    COMMAND cargo build --release --target wasm32-unknown-unknown --workspace
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/test/wasm_fixtures
    DEPENDS ${WASM_SOURCE_FILES}
    COMMENT "Building Rust WASM module"
)

add_custom_target(wasm_fixtures ALL
    DEPENDS ${CMAKE_BINARY_DIR}/wasm
)

add_custom_command(
    TARGET wasm_fixtures POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/wasm
    COMMAND ${CMAKE_COMMAND} -E copy
            ${RUST_WASM_OUTPUT_DIR}/*.wasm
            ${CMAKE_BINARY_DIR}/wasm
    COMMENT "Copying .wasm to build directory"
)

set(EMBED_HEADER ${CMAKE_BINARY_DIR}/wasm_test_fixtures.h)

# 2. Glob all .wasm files in the output folder
file(GLOB WASM_FILES ${CMAKE_BINARY_DIR}/wasm/*.wasm)

# 3. Start building up the embedding command
add_custom_command(
  OUTPUT ${EMBED_HEADER}
  COMMAND ${CMAKE_COMMAND} -E echo "// Embedded WASM fixtures" > ${EMBED_HEADER}
  COMMENT "Embedding all WASM testcases into a single header"
)

# 4. For each .wasm file, add a `xxd -i -n ...` line to the custom command
foreach(wasm_file ${WASM_FILES})
  get_filename_component(wasm_name ${wasm_file} NAME_WE) # e.g. "kyc"
  add_custom_command(
    OUTPUT ${EMBED_HEADER}
    APPEND
    COMMAND xxd -i -n wasm_${wasm_name} ${wasm_file} >> ${EMBED_HEADER}
  )
endforeach()

add_custom_target(embed_wasm_header ALL
  DEPENDS ${EMBED_HEADER}
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
