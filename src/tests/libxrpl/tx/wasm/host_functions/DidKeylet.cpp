#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/RealHostFixture.h>

#include <expected>
#include <iterator>

namespace xrpl::test {

struct DidKeyletImpl : WasmImplTest
{
};

TEST_F(DidKeyletImpl, MatchesDidKeyletFunction)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));

    auto const expected = keylet::did(owner.id());
    auto const expectedBytes = Bytes{std::begin(expected.key), std::end(expected.key)};
    auto const result = host().didKeylet(owner.id());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, expectedBytes);
}

TEST_F(DidKeyletImpl, InvalidAccount)
{
    auto result = host().didKeylet(AccountID{});
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
