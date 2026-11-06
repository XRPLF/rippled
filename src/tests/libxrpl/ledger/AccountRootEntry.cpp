#include <xrpl/ledger/entries/AccountRootEntry.h>

#include <tests/libxrpl/helpers/Account.h>
#include <tests/libxrpl/ledger/EntryTestHelpers.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>

namespace xrpl::test {

TEST(AccountRootEntryTests, constructors)
{
    EntryTestEnv e;

    expectKeylet<AccountRootEntry>(e, keylet::account(e.alice.id()), "account(id)", e.alice.id());

    expectKeylet<AccountRootEntry>(
        e, keylet::account(Account("nobody").id()), "account(id) absent", Account("nobody").id());
}

}  // namespace xrpl::test
