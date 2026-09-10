#include <xrpl/ledger/helpers/CredentialEntry.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

#include <string>

namespace xrpl::test {

TEST(CredentialEntryTests, Constructors)
{
    EntryTestEnv e;

    std::string const credTypeStr = "termsandconditions";
    Slice const credType = makeSlice(credTypeStr);

    expectKeylet<CredentialEntry>(
        e,
        keylet::credential(e.alice.id(), e.bob.id(), credType),
        "credential(subject, issuer, credType)",
        e.alice.id(),
        e.bob.id(),
        credType);

    expectKeylet<CredentialEntry>(
        e, keylet::credential(e.someID()), "credential(uint256)", e.someID());

    // Subject and issuer are both AccountIDs, so the assertion above only
    // has teeth if their order matters.
    EXPECT_NE(
        keylet::credential(e.alice.id(), e.bob.id(), credType).key,
        keylet::credential(e.bob.id(), e.alice.id(), credType).key);
}

}  // namespace xrpl::test
