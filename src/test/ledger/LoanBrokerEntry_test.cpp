#include <test/jtx/Account.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/LoanBrokerEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>

namespace xrpl::test {

class LoanBrokerEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        SeqProxy const seq = SeqProxy::rawSequence(5);

        expectKeylet<LoanBrokerEntry>(
            *this,
            e,
            keylet::loanBroker(e.alice.id(), seq),
            "loanBroker(owner, seq)",
            e.alice.id(),
            seq);

        expectKeylet<LoanBrokerEntry>(
            *this, e, keylet::loanBroker(e.someID()), "loanBroker(uint256)", e.someID());
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(LoanBrokerEntry, ledger, xrpl);

}  // namespace xrpl::test
