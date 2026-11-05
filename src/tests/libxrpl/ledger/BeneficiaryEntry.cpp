#include <xrpl/ledger/entries/BeneficiaryEntry.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(BeneficiaryEntryTests, constructors)
{
    EntryTestEnv e;

    expectKeylet<BeneficiaryEntry>(
        e, keylet::beneficiary(e.alice.id()), "beneficiary(account)", e.alice.id());
}

}  // namespace xrpl::test
