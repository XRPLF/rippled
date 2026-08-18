#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct TrustlineKeyletImpl : WasmImplTest
{
};

TEST_F(TrustlineKeyletImpl, MatchesTrustlineKeyletFunction)
{
    auto const owner = fund("owner");
    auto const destination = fund("destination");

    auto const usd = toCurrency("USD");

    expectKeyletMatches(
        makeHost()->trustLineKeylet(owner.id(), destination.id(), usd),
        keylet::trustLine(owner.id(), destination.id(), usd));
}

TEST_F(TrustlineKeyletImpl, InvalidCurrency)
{
    auto const owner = fund("owner");
    auto const destination = fund("destination");

    expectError(
        makeHost()->trustLineKeylet(owner.id(), destination.id(), toCurrency("")),
        HostFunctionError::InvalidParams);
}

TEST_F(TrustlineKeyletImpl, CantTrustlineToSelf)
{
    auto const owner = fund("owner");

    auto const usd = toCurrency("USD");

    expectError(
        makeHost()->trustLineKeylet(owner.id(), owner.id(), usd), HostFunctionError::InvalidParams);
}

TEST_F(TrustlineKeyletImpl, InvalidAccount)
{
    auto const owner = fund("owner");

    auto const usd = toCurrency("USD");

    auto h = makeHost();

    expectError(
        h->trustLineKeylet(AccountID{}, owner.id(), usd), HostFunctionError::InvalidAccount);

    expectError(
        h->trustLineKeylet(owner.id(), AccountID{}, usd), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
