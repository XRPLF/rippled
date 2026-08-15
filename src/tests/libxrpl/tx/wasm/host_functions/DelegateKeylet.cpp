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

struct DelegateKeyletImpl : WasmImplTest
{
};

TEST_F(DelegateKeyletImpl, MatchesDelegateKeyletFunction)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    auto const delegate = Account{"delegate"};
    ledger.createAccount(delegate, XRP(1000));

    auto const expected = keylet::delegate(owner.id(), delegate.id());
    auto const expectedBytes = Bytes{std::begin(expected.key), std::end(expected.key)};
    auto const result = host().delegateKeylet(owner.id(), delegate.id());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, expectedBytes);
}

TEST_F(DelegateKeyletImpl, CantDelegateToSelf)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));

    auto const result = host().delegateKeylet(owner.id(), owner.id());
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidParams);
}

TEST_F(DelegateKeyletImpl, InvalidAccount)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));

    auto result = host().delegateKeylet(AccountID{}, owner.id());
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidAccount);

    result = host().delegateKeylet(owner.id(), AccountID{});
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
