#include <xrpl/beast/unit_test/suite.h>

#include <rs_hello_world_cxxbridge/lib.h>

namespace xrpl {

class RustInterop_test : public beast::unit_test::suite
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
    testLogInfo()
    {
        testcase("log_info");
        auto const guard = rs::hello_world::init_logger();
        rs::hello_world::log_info("test log message from C++");
        BEAST_EXPECT(true);
        // Second init should panic; safe_init_logger catches it and throws.
        bool caught = false;
        try
        {
            rs::hello_world::safe_init_logger();
        }
        catch (std::exception const&)
        {
            caught = true;
        }
        BEAST_EXPECT(caught);
    }

    void
    run() override
    {
        testHelloWorld();
        testLogInfo();
    }
};

BEAST_DEFINE_TESTSUITE(RustInterop, basics, xrpl);

}  // namespace xrpl
