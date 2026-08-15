#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/RealHostFixture.h>

#include <expected>
#include <iterator>

namespace xrpl::test {

struct AmmKeyletImpl : WasmImplTest
{
};

TEST_F(AmmKeyletImpl, MatchesAmmKeyletFunction)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));

    auto usdIssue = Issue{toCurrency("USD"), owner.id()};

    auto const expected = keylet::amm(xrpIssue(), usdIssue);
    auto const expectedBytes = Bytes{std::begin(expected.key), std::end(expected.key)};
    auto const result = host().ammKeylet(usdIssue, xrpIssue());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, expectedBytes);
}

TEST_F(AmmKeyletImpl, InvalidIssue1)
{
    auto const result = host().ammKeylet(xrpIssue(), xrpIssue());
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidParams);
}

TEST_F(AmmKeyletImpl, InvalidIssue2)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));

    auto baseMpt = makeMptID(1, owner.id());

    auto const result = host().ammKeylet(baseMpt, xrpIssue());
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidParams);
}

}  // namespace xrpl::test
