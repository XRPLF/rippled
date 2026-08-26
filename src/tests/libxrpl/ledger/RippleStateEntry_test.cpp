#include <xrpl/ledger/helpers/RippleStateEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/UintTypes.h>

#include <gtest/gtest.h>
#include <helpers/IOU.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(RippleStateEntryTests, Constructors)
{
    EntryTestEnv e;

    IOU const usd("USD", e.alice);
    Currency const currency = usd.currency();

    expectKeylet<RippleStateEntry>(
        e,
        keylet::trustLine(e.alice.id(), e.bob.id(), currency),
        "trustLine(id0, id1, currency)",
        e.alice.id(),
        e.bob.id(),
        currency);

    expectKeylet<RippleStateEntry>(
        e,
        keylet::trustLine(e.bob.id(), usd.issue()),
        "trustLine(id, issue)",
        e.bob.id(),
        usd.issue());

    // Trust lines are deliberately symmetric in their two accounts -- the
    // keylet canonicalizes them -- so unlike the other two-account entries
    // there is no transposition to catch here.
    EXPECT_EQ(
        keylet::trustLine(e.alice.id(), e.bob.id(), currency).key,
        keylet::trustLine(e.bob.id(), e.alice.id(), currency).key);
}

}  // namespace xrpl::test
