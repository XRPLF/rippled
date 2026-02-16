include(XrplAddTest)

# Test requirements.
find_package(GTest REQUIRED)

# Common library dependencies for xrpld tests.
add_library(xrpld.imports.test INTERFACE)
target_link_libraries(xrpld.imports.test INTERFACE 
    gtest::gtest 
    xrpl.libxrpl
)

# gRPC tests
xrpl_add_test(grpc)
target_link_libraries(xrpl.test.grpc PRIVATE xrpld.imports.test)
add_dependencies(xrpl.tests xrpl.test.grpc)

