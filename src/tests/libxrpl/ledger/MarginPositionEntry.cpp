#include <xrpl/ledger/entries/MarginPositionEntry.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/UintTypes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(MarginPositionEntryTests, constructors)
{
    EntryTestEnv e;

    auto const marketID = keylet::perpMarket(e.someID(), xrpCurrency(), toCurrency("USD")).key;

    expectKeylet<MarginPositionEntry>(
        e,
        keylet::marginPosition(e.someID(), marketID),
        "marginPosition(marginAccountID, marketID)",
        e.someID(),
        marketID);

    expectKeylet<MarginPositionEntry>(
        e, keylet::marginPosition(e.someID()), "marginPosition(UInt256)", e.someID());
}

}  // namespace xrpl::test
