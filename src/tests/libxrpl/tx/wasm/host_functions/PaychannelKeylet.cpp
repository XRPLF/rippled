#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/RealHostFixture.h>

#include <expected>
#include <iterator>

namespace xrpl::test {

struct PaychannelKeyletImpl : WasmImplTest
{
};

TEST_F(PaychannelKeyletImpl, MatchesPaychannelFunction)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    auto const destination = Account{"destination"};
    ledger.createAccount(destination, XRP(1000));

    auto const expected =
        keylet::payChannel(owner.id(), destination.id(), SeqProxy::rawSequence(1u));
    auto const expectedBytes = Bytes{std::begin(expected.key), std::end(expected.key)};
    auto const result = host().paychannelKeylet(owner.id(), destination.id(), 1u);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, expectedBytes);
}

TEST_F(PaychannelKeyletImpl, CantUseSelf)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));

    auto result = host().paychannelKeylet(owner.id(), owner.id(), 1u);
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidParams);
}

TEST_F(PaychannelKeyletImpl, InvalidAccount)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));

    auto result = host().paychannelKeylet(AccountID{}, owner.id(), 1u);
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidAccount);

    result = host().paychannelKeylet(owner.id(), AccountID{}, 1u);
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
