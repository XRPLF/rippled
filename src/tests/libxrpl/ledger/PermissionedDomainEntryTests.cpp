#include <xrpl/ledger/helpers/PermissionedDomainEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(PermissionedDomainEntryTests, Constructors)
{
    EntryTestEnv e;

    SeqProxy const seq = SeqProxy::rawSequence(6);

    expectKeylet<PermissionedDomainEntry>(
        e,
        keylet::permissionedDomain(e.alice.id(), seq),
        "permissionedDomain(account, seq)",
        e.alice.id(),
        seq);

    expectKeylet<PermissionedDomainEntry>(
        e, keylet::permissionedDomain(e.someID()), "permissionedDomain(uint256)", e.someID());
}

}  // namespace xrpl::test
