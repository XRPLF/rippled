#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/LoanEntry.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SeqProxy.h>

namespace xrpl {
namespace test {

class LoanEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        SeqProxy const seq = SeqProxy::rawSequence(9);

        expectKeylet<LoanEntry>(
            *this,
            e,
            keylet::loan(e.someID(), seq),
            "loan(loanBrokerID, loanSeq)",
            e.someID(),
            seq);

        expectKeylet<LoanEntry>(*this, e, keylet::loan(e.someID()), "loan(uint256)", e.someID());

        // Both overloads start with the same uint256, so they must not produce
        // the same key -- otherwise arity is the only thing keeping them apart
        // and the test proves nothing.
        BEAST_EXPECT(keylet::loan(e.someID(), seq).key != keylet::loan(e.someID()).key);
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(LoanEntry, ledger, xrpl);

}  // namespace test
}  // namespace xrpl
