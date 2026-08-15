#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/RealHostFixture.h>

#include <expected>
#include <iterator>

namespace xrpl::test {

struct TrustlineKeyletImpl : WasmImplTest
{
};

TEST_F(TrustlineKeyletImpl, MatchesTrustlineKeyletFunction)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    auto const destination = Account{"destination"};
    ledger.createAccount(destination, XRP(1000));

    auto const usd = toCurrency("USD");

    auto const expected = keylet::trustLine(owner.id(), destination.id(), usd);
    auto const expectedBytes = Bytes{std::begin(expected.key), std::end(expected.key)};
    auto const result = host().trustLineKeylet(owner.id(), destination.id(), usd);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, expectedBytes);
}

TEST_F(TrustlineKeyletImpl, InvalidCurrency)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    auto const destination = Account{"destination"};
    ledger.createAccount(destination, XRP(1000));

    auto const result = host().trustLineKeylet(owner.id(), destination.id(), toCurrency(""));
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidParams);
}

TEST_F(TrustlineKeyletImpl, CantTrustlineToSelf)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));

    auto const usd = toCurrency("USD");

    auto const result = host().trustLineKeylet(owner.id(), owner.id(), usd);
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidParams);
}

TEST_F(TrustlineKeyletImpl, InvalidAccount)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));

    auto const usd = toCurrency("USD");

    auto result = host().trustLineKeylet(AccountID{}, owner.id(), usd);
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidAccount);

    result = host().trustLineKeylet(owner.id(), AccountID{}, usd);
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
