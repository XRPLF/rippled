#include <xrpl/beast/unit_test/suite.h>

#include <rs_hello_world_cxxbridge/lib.h>

namespace xrpl {

class RustInterop_test : public beast::unit_test::Suite
{
public:
    void
    testHelloWorld()
    {
        testcase("hello_world");
        auto result = rs::hello_world::hello_world();
        BEAST_EXPECT(result == "hello_world");
    }

    void
    run() override
    {
        testHelloWorld();
    }
};

BEAST_DEFINE_TESTSUITE(RustInterop, basics, xrpl);

}  // namespace xrpl
