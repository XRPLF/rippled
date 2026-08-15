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

struct DepositPreauthKeyletImpl : WasmImplTest
{
};

TEST_F(DepositPreauthKeyletImpl, MatchesDepositPreauthKeyletFunction)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    auto const destination = Account{"destination"};
    ledger.createAccount(destination, XRP(1000));

    auto const expected = keylet::depositPreauth(owner.id(), destination.id());
    auto const expectedBytes = Bytes{std::begin(expected.key), std::end(expected.key)};
    auto const result = host().depositPreauthKeylet(owner.id(), destination.id());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, expectedBytes);
}

TEST_F(DepositPreauthKeyletImpl, CantPreauthToSelf)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));

    auto const result = host().depositPreauthKeylet(owner.id(), owner.id());
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidParams);
}

TEST_F(DepositPreauthKeyletImpl, InvalidAccount)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));

    auto result = host().depositPreauthKeylet(AccountID{}, owner.id());
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidAccount);

    result = host().depositPreauthKeylet(owner.id(), AccountID{});
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
