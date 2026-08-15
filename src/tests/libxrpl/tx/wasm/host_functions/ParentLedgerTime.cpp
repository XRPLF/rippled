#include <gtest/gtest.h>
#include <tx/wasm/RealHostFixture.h>

#include <expected>

namespace xrpl::test {

struct ParentLedgerTimeImpl : WasmImplTest
{
};

TEST_F(ParentLedgerTimeImpl, MatchesLedger)
{
    auto const result = host().getParentLedgerTime();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, ledger.getOpenLedger().parentCloseTime().time_since_epoch().count());
}

}  // namespace xrpl::test
