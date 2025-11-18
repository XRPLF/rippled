#ifndef XRPL_RPC_TESTOUTPUTSUITE_H_INCLUDED
#define XRPL_RPC_TESTOUTPUTSUITE_H_INCLUDED

#include <test/jtx/TestSuite.h>

#include <xrpl/json/Output.h>
#include <xrpl/json/Writer.h>

namespace ripple {
namespace test {

class TestOutputSuite : public TestSuite
{
protected:
    std::string output_;
    std::unique_ptr<Json::Writer> writer_;

    void
    setup(std::string const& testName)
    {
        testcase(testName);
        output_.clear();
        writer_ = std::make_unique<Json::Writer>(Json::stringOutput(output_));
    }

    // Test the result and report values.
    void
    expectResult(std::string const& expected, std::string const& message = "")
    {
        writer_.reset();

        expectEquals(output_, expected, message);
    }
};

}  // namespace test
}  // namespace ripple

#endif
