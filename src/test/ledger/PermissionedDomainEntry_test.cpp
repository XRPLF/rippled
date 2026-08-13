#include <test/jtx/Account.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/PermissionedDomainEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>

namespace xrpl::test {

class PermissionedDomainEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        SeqProxy const seq = SeqProxy::rawSequence(6);

        expectKeylet<PermissionedDomainEntry>(
            *this,
            e,
            keylet::permissionedDomain(e.alice.id(), seq),
            "permissionedDomain(account, seq)",
            e.alice.id(),
            seq);

        expectKeylet<PermissionedDomainEntry>(
            *this,
            e,
            keylet::permissionedDomain(e.someID()),
            "permissionedDomain(uint256)",
            e.someID());
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(PermissionedDomainEntry, ledger, xrpl);

}  // namespace xrpl::test
