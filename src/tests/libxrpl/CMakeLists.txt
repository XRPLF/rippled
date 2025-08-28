include(xrpl_add_test)

# Test requirements.
find_package(doctest REQUIRED)
find_package(RapidJSON REQUIRED)

# Common library dependencies for the rest of the tests.
add_library(xrpl.imports.test INTERFACE)
target_link_libraries(xrpl.imports.test INTERFACE doctest::doctest rapidjson xrpl.libxrpl)

# One test for each module.
xrpl_add_test(basics)
target_link_libraries(xrpl.test.basics PRIVATE xrpl.imports.test)
xrpl_add_test(crypto)
target_link_libraries(xrpl.test.crypto PRIVATE xrpl.imports.test)
