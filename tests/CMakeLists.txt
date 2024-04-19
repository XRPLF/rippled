include(xrpl_add_test)

# Test requirements.
find_package(doctest REQUIRED)

# One-off test for rippled.
add_test(NAME rippled_build COMMAND
  "${CMAKE_COMMAND}"
  --build "${CMAKE_BINARY_DIR}"
  --config "$<CONFIG>"
  --target rippled
)
set_tests_properties(rippled_build PROPERTIES FIXTURES_SETUP rippled_fixture)
add_test(
  NAME rippled
  COMMAND rippled --unittest
    "$<$<BOOL:${RIPPLED_UNITTEST_JOBS}>:--unittest-jobs;${RIPPLED_UNITTEST_JOBS}>"
  COMMAND_EXPAND_LISTS
)
set_tests_properties(rippled PROPERTIES FIXTURES_REQUIRED rippled_fixture)

# Common library dependencies for the rest of the tests.
add_library(xrpl.imports.test INTERFACE)
target_link_libraries(xrpl.imports.test INTERFACE doctest::doctest xrpl.libxrpl)

# One test for each module.
xrpl_add_test(basics)
target_link_libraries(xrpl.test.basics PRIVATE xrpl.imports.test)
xrpl_add_test(crypto)
target_link_libraries(xrpl.test.crypto PRIVATE xrpl.imports.test)
