#include <test/jtx/Account.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/NFTokenOfferEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>

namespace xrpl::test {

class NFTokenOfferEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        SeqProxy const seq = SeqProxy::rawSequence(13);

        expectKeylet<NFTokenOfferEntry>(
            *this,
            e,
            keylet::nftokenOffer(e.alice.id(), seq),
            "nftokenOffer(owner, seq)",
            e.alice.id(),
            seq);

        expectKeylet<NFTokenOfferEntry>(
            *this, e, keylet::nftokenOffer(e.someID()), "nftokenOffer(uint256)", e.someID());
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(NFTokenOfferEntry, ledger, xrpl);

}  // namespace xrpl::test
