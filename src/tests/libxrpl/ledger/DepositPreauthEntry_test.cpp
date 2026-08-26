#include <xrpl/basics/Slice.h>
#include <xrpl/ledger/helpers/DepositPreauthEntry.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

#include <set>
#include <string>
#include <utility>

namespace xrpl::test {

TEST(DepositPreauthEntryTests, Constructors)
{
    EntryTestEnv e;

    std::string const credTypeStr = "termsandconditions";
    std::set<std::pair<AccountID, Slice>> const authCreds{{e.bob.id(), makeSlice(credTypeStr)}};

    expectKeylet<DepositPreauthEntry>(
        e,
        keylet::depositPreauth(e.alice.id(), e.bob.id()),
        "depositPreauth(owner, preauthorized)",
        e.alice.id(),
        e.bob.id());

    expectKeylet<DepositPreauthEntry>(
        e,
        keylet::depositPreauth(e.alice.id(), authCreds),
        "depositPreauth(owner, authCreds)",
        e.alice.id(),
        authCreds);

    expectKeylet<DepositPreauthEntry>(
        e, keylet::depositPreauth(e.someID()), "depositPreauth(uint256)", e.someID());

    // Owner and preauthorized are both AccountIDs, so the assertion above
    // only has teeth if their order matters.
    EXPECT_NE(
        keylet::depositPreauth(e.alice.id(), e.bob.id()).key,
        keylet::depositPreauth(e.bob.id(), e.alice.id()).key);

    // The credential-set overload must not collide with the single-account
    // one.
    EXPECT_NE(
        keylet::depositPreauth(e.alice.id(), authCreds).key,
        keylet::depositPreauth(e.alice.id(), e.bob.id()).key);
}

}  // namespace xrpl::test
