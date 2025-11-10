include(XrplAddTest)

# Test requirements.
find_package(doctest REQUIRED)

# Common library dependencies for the rest of the tests.
add_library(xrpl.imports.test INTERFACE)
target_link_libraries(xrpl.imports.test INTERFACE doctest::doctest xrpl.libxrpl)

# One test for each module.
xrpl_add_test(basics)
target_link_libraries(xrpl.test.basics PRIVATE xrpl.imports.test)
xrpl_add_test(crypto)
target_link_libraries(xrpl.test.crypto PRIVATE xrpl.imports.test)
xrpl_add_test(json)
target_link_libraries(xrpl.test.json PRIVATE xrpl.imports.test)

# Network unit tests are currently not supported on Windows
if(NOT WIN32)
  xrpl_add_test(net)
  target_link_libraries(xrpl.test.net PRIVATE xrpl.imports.test)
endif()
