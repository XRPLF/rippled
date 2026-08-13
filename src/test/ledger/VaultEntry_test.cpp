#include <test/jtx/Account.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/VaultEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>

namespace xrpl::test {

class VaultEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        SeqProxy const seq = SeqProxy::rawSequence(8);

        expectKeylet<VaultEntry>(
            *this, e, keylet::vault(e.alice.id(), seq), "vault(owner, seq)", e.alice.id(), seq);

        expectKeylet<VaultEntry>(*this, e, keylet::vault(e.someID()), "vault(uint256)", e.someID());
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(VaultEntry, ledger, xrpl);

}  // namespace xrpl::test
