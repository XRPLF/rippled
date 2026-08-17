#include <gtest/gtest.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct BaseFeeImpl : WasmImplTest
{
};

TEST_F(BaseFeeImpl, MatchesLedger)
{
    expectValue(makeHost()->getBaseFee(), ledger.getOpenLedger().fees().base.drops());
}

}  // namespace xrpl::test
