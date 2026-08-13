#include <test/jtx/Account.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/OfferEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>

namespace xrpl::test {

class OfferEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        SeqProxy const seq = SeqProxy::rawSequence(3);

        expectKeylet<OfferEntry>(
            *this, e, keylet::offer(e.alice.id(), seq), "offer(id, seq)", e.alice.id(), seq);

        expectKeylet<OfferEntry>(*this, e, keylet::offer(e.someID()), "offer(uint256)", e.someID());
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(OfferEntry, ledger, xrpl);

}  // namespace xrpl::test
