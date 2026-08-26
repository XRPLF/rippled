#include <xrpl/ledger/helpers/AccountRootEntry.h>
#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(AccountRootEntryTests, Constructors)
{
    EntryTestEnv e;

    expectKeylet<AccountRootEntry>(e, keylet::account(e.alice.id()), "account(id)", e.alice.id());

    expectKeylet<AccountRootEntry>(
        e, keylet::account(Account("nobody").id()), "account(id) absent", Account("nobody").id());
}

}  // namespace xrpl::test
