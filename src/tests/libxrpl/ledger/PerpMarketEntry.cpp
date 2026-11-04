#include <xrpl/ledger/entries/PerpMarketEntry.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/UintTypes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(PerpMarketEntryTests, constructors)
{
    EntryTestEnv e;

    auto const usd = toCurrency("USD");

    expectKeylet<PerpMarketEntry>(
        e,
        keylet::perpMarket(e.someID(), xrpCurrency(), usd),
        "perpMarket(exchangeID, base, quote)",
        e.someID(),
        xrpCurrency(),
        usd);

    expectKeylet<PerpMarketEntry>(
        e, keylet::perpMarket(e.someID()), "perpMarket(UInt256)", e.someID());
}

}  // namespace xrpl::test
