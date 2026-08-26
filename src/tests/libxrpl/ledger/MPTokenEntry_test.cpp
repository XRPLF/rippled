#include <xrpl/ledger/helpers/MPTokenEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/UintTypes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(MPTokenEntryTests, Constructors)
{
    EntryTestEnv e;

    MPTID const issuanceID = makeMptID(1, e.alice.id());

    expectKeylet<MPTokenEntry>(
        e,
        keylet::mptoken(issuanceID, e.bob.id()),
        "mptoken(MPTID, holder)",
        issuanceID,
        e.bob.id());

    expectKeylet<MPTokenEntry>(
        e,
        keylet::mptoken(e.someID(), e.bob.id()),
        "mptoken(issuanceKey, holder)",
        e.someID(),
        e.bob.id());

    expectKeylet<MPTokenEntry>(e, keylet::mptoken(e.someID()), "mptoken(uint256)", e.someID());
}

}  // namespace xrpl::test
