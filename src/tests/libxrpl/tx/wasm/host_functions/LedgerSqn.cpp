#include <gtest/gtest.h>
#include <tx/wasm/RealHostFixture.h>

#include <expected>

namespace xrpl::test {

struct LedgerSqnImpl : WasmImplTest
{
};

TEST_F(LedgerSqnImpl, MatchesLedger)
{
    auto const result = host().getLedgerSqn();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, ledger.getOpenLedger().header().seq);
}

}  // namespace xrpl::test
