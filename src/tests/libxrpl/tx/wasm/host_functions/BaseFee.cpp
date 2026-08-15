#include <gtest/gtest.h>
#include <tx/wasm/RealHostFixture.h>

#include <expected>

namespace xrpl::test {

struct BaseFeeImpl : WasmImplTest
{
};

TEST_F(BaseFeeImpl, MatchesLedger)
{
    auto const result = host().getBaseFee();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, ledger.getOpenLedger().fees().base.drops());
}

}  // namespace xrpl::test
