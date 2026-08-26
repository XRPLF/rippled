#include <xrpl/ledger/helpers/NFTokenOfferEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(NFTokenOfferEntryTests, Constructors)
{
    EntryTestEnv e;

    SeqProxy const seq = SeqProxy::rawSequence(13);

    expectKeylet<NFTokenOfferEntry>(
        e, keylet::nftokenOffer(e.alice.id(), seq), "nftokenOffer(owner, seq)", e.alice.id(), seq);

    expectKeylet<NFTokenOfferEntry>(
        e, keylet::nftokenOffer(e.someID()), "nftokenOffer(uint256)", e.someID());
}

}  // namespace xrpl::test
