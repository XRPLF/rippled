/**
 * @file
 * @brief Tests for RWDBDatabase - in-memory RelationalDatabase implementation.
 */

#include <xrpld/app/rdb/backend/RWDBDatabase.h>

#include <xrpl/beast/unit_test.h>

namespace xrpl::test {

/**
 * Test RWDBDatabase implementation of RelationalDatabase.
 *
 * Note: These tests require a mock Application and ServiceRegistry,
 * so we focus on the interface-level behavior that can be tested
 * without full application infrastructure.
 *
 * For integration tests with full Application, see SHAMapStore_test.cpp
 * and Backend_test.cpp.
 */
class RWDBDatabase_test : public beast::unit_test::Suite
{
public:
    void
    run() override
    {
        testDesignVerification();
    }

    void
    testDesignVerification()
    {
        testcase("design verification");

        // Verify that RWDBDatabase implements RelationalDatabase
        // This is a compile-time check - if it compiles, the interface is satisfied
        static_assert(
            std::is_base_of_v<RelationalDatabase, RWDBDatabase>,
            "RWDBDatabase must implement RelationalDatabase");

        pass();
    }
};

BEAST_DEFINE_TESTSUITE(RWDBDatabase, app, xrpl);

}  // namespace xrpl::test
