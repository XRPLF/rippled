#include <gtest/gtest.h>
#include <tx/wasm/RealHostFixture.h>

#include <expected>

namespace xrpl::test {

struct ParentLedgerHashImpl : WasmImplTest
{
};

TEST_F(ParentLedgerHashImpl, MatchesLedger)
{
    auto const result = host().getParentLedgerHash();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, ledger.getOpenLedger().header().parentHash);
}

}  // namespace xrpl::test
