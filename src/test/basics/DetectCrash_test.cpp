#include <xrpl/beast/unit_test/suite.h>

#include <exception>

namespace ripple {
namespace test {

struct DetectCrash_test : public beast::unit_test::suite
{
    void
    testDetectCrash()
    {
        testcase("Detect Crash");
        // Kill the process. This is used to test that the multi-process
        // unit test will correctly report the crash.
        std::terminate();
    }
    void
    run() override
    {
        testDetectCrash();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(DetectCrash, basics, beast);

}  // namespace test
}  // namespace ripple
