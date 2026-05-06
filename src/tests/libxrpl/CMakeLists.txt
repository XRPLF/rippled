include(XrplAddTest)

# Test requirements.
find_package(GTest REQUIRED)

# Custom target for all tests defined in this file
add_custom_target(xrpl.tests)

# Test helpers
add_library(xrpl.helpers.test STATIC)
target_sources(
    xrpl.helpers.test
    PRIVATE helpers/Account.cpp helpers/TestSink.cpp helpers/TxTest.cpp
)
target_include_directories(xrpl.helpers.test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xrpl.helpers.test PUBLIC xrpl.libxrpl gtest::gtest)

# Common library dependencies for the rest of the tests.
add_library(xrpl.imports.test INTERFACE)
target_link_libraries(
    xrpl.imports.test
    INTERFACE gtest::gtest xrpl.libxrpl xrpl.helpers.test
)

# One test for each module.
xrpl_add_test(basics)
target_link_libraries(xrpl.test.basics PRIVATE xrpl.imports.test)
add_dependencies(xrpl.tests xrpl.test.basics)

xrpl_add_test(crypto)
target_link_libraries(xrpl.test.crypto PRIVATE xrpl.imports.test)
add_dependencies(xrpl.tests xrpl.test.crypto)

xrpl_add_test(json)
target_link_libraries(xrpl.test.json PRIVATE xrpl.imports.test)
add_dependencies(xrpl.tests xrpl.test.json)

xrpl_add_test(
    peerfinder
    ${CMAKE_SOURCE_DIR}/src/xrpld/core/detail/Config.cpp
    ${CMAKE_SOURCE_DIR}/src/xrpld/peerfinder/detail/Bootcache.cpp
    ${CMAKE_SOURCE_DIR}/src/xrpld/peerfinder/detail/Endpoint.cpp
    ${CMAKE_SOURCE_DIR}/src/xrpld/peerfinder/detail/PeerfinderConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/xrpld/peerfinder/detail/SlotImp.cpp
)
target_include_directories(xrpl.test.peerfinder PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(
    xrpl.test.peerfinder
    PRIVATE xrpl.imports.test Xrpl::boost Xrpl::libs Xrpl::opts
)
add_dependencies(xrpl.tests xrpl.test.peerfinder)

xrpl_add_test(tx)
target_link_libraries(xrpl.test.tx PRIVATE xrpl.imports.test)
add_dependencies(xrpl.tests xrpl.test.tx)

xrpl_add_test(protocol_autogen)
target_link_libraries(xrpl.test.protocol_autogen PRIVATE xrpl.imports.test)
add_dependencies(xrpl.tests xrpl.test.protocol_autogen)

# Network unit tests are currently not supported on Windows
if(NOT WIN32)
    xrpl_add_test(net)
    target_link_libraries(xrpl.test.net PRIVATE xrpl.imports.test)
    add_dependencies(xrpl.tests xrpl.test.net)
endif()
