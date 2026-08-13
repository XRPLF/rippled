#include <test/jtx/Account.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/EscrowEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>

namespace xrpl::test {

class EscrowEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        SeqProxy const seq = SeqProxy::rawSequence(11);

        expectKeylet<EscrowEntry>(
            *this, e, keylet::escrow(e.alice.id(), seq), "escrow(src, seq)", e.alice.id(), seq);
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(EscrowEntry, ledger, xrpl);

}  // namespace xrpl::test
