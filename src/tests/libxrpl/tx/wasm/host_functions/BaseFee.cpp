#include <gtest/gtest.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct BaseFeeImpl : RealHostFixture
{
};

TEST_F(BaseFeeImpl, matches_ledger)
{
    expectValue(makeHost()->getBaseFee(), ledger.getOpenLedger().fees().base.drops());
}

}  // namespace xrpl::test
