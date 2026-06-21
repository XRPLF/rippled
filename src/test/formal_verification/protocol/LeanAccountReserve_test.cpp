#include <test/formal_verification/common/LeanSuite.h>
#include <test/formal_verification/ffi/protocol/FeesFFI.h>
#include <test/jtx.h>
#include <test/jtx/envconfig.h>

#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <limits>
#include <sstream>

namespace xrpl::test {

using namespace formal_verification;

class LeanAccountReserve_test : public LeanSuite
{
    void
    runAccountReserve(
        int64_t reserve,
        int64_t increment,
        uint32_t ownerCount,
        int64_t expected,
        char const* label)
    {
        using namespace jtx;
        Env env(*this, envconfig([=](std::unique_ptr<Config> cfg) {
            cfg->fees.accountReserve = XRPAmount{reserve};
            cfg->fees.ownerReserve = XRPAmount{increment};
            return cfg;
        }));
        auto const& fees = env.current()->fees();

        beginCase(label);
        int64_t const cpp = fees.accountReserve(ownerCount).drops();
        FeesFFI const leanFees = FeesFFI::build(fees.base, fees.reserve, fees.increment);
        int64_t const lean = leanFees.accountReserve(ownerCount).drops();
        if (cpp != expected || lean != expected)
        {
            std::stringstream ss;
            ss << label << ": accountReserve(" << ownerCount << ") expected=" << expected
               << " cpp=" << cpp << " lean=" << lean;
            fail(ss.str());
            return;
        }
        pass();
    }

    void
    testAccountReserve()
    {
        constexpr int64_t int64max = std::numeric_limits<int64_t>::max();
        constexpr int64_t int64min = std::numeric_limits<int64_t>::min();
        constexpr uint32_t u32max = std::numeric_limits<uint32_t>::max();

        // known values
        runAccountReserve(15, 33, 50'000'000, 1'650'000'015, "accountReserve.known_1");
        runAccountReserve(33, 50'000'000, 12, 600'000'033, "accountReserve.known_2");
        runAccountReserve(50'000'000, 15, 33, 50'000'495, "accountReserve.known_3");

        // extreme values
        runAccountReserve(int64min, int64min, 0, int64min, "accountReserve.min_min_0");
        // overflow case: u32max*min wraps to min and min+min wraps to 0
        runAccountReserve(int64min, int64min, u32max, 0, "accountReserve.min_min_max");
        runAccountReserve(int64min, 0, 0, int64min, "accountReserve.min_0_0");
        runAccountReserve(int64min, 0, u32max, int64min, "accountReserve.min_0_max");
        runAccountReserve(int64min, int64max, 0, int64min, "accountReserve.min_max_0");
        // overflow case: -(2^32 - 1)
        runAccountReserve(int64min, int64max, u32max, -4'294'967'295, "accountReserve.min_max_max");
        runAccountReserve(0, int64min, 0, 0, "accountReserve.0_min_0");
        runAccountReserve(0, int64min, u32max, int64min, "accountReserve.0_min_max");
        runAccountReserve(0, 0, 0, 0, "accountReserve.0_0_0");
        runAccountReserve(0, 0, u32max, 0, "accountReserve.0_0_max");
        runAccountReserve(0, int64max, 0, 0, "accountReserve.0_max_0");
        // overflow case: 2^63 - 2^32 + 1
        runAccountReserve(
            0, int64max, u32max, 9'223'372'032'559'808'513, "accountReserve.0_max_max");
        runAccountReserve(int64max, int64min, 0, int64max, "accountReserve.max_min_0");
        // overflow case: max + (u32max*min -> min) = -1
        runAccountReserve(int64max, int64min, u32max, -1, "accountReserve.max_min_max");
        runAccountReserve(int64max, 0, 0, int64max, "accountReserve.max_0_0");
        runAccountReserve(int64max, 0, u32max, int64max, "accountReserve.max_0_max");
        runAccountReserve(int64max, int64max, 0, int64max, "accountReserve.max_max_0");
        // overflow case: -2^32
        runAccountReserve(int64max, int64max, u32max, -4'294'967'296, "accountReserve.max_max_max");
    }

    void
    runTests() override
    {
        testAccountReserve();
    }
};

BEAST_DEFINE_TESTSUITE(LeanAccountReserve, formal_verification, xrpl);

}  // namespace xrpl::test
