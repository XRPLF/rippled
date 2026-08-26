#include <xrpl/ledger/helpers/PayChannelEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SeqProxy.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(PayChannelEntryTests, Constructors)
{
    EntryTestEnv e;

    SeqProxy const seq = SeqProxy::rawSequence(4);

    expectKeylet<PayChannelEntry>(
        e,
        keylet::payChannel(e.alice.id(), e.bob.id(), seq),
        "payChannel(src, dst, seq)",
        e.alice.id(),
        e.bob.id(),
        seq);

    // Source and destination are both AccountIDs, so the assertion above
    // only has teeth if their order matters.
    EXPECT_NE(
        keylet::payChannel(e.alice.id(), e.bob.id(), seq).key,
        keylet::payChannel(e.bob.id(), e.alice.id(), seq).key);
}

}  // namespace xrpl::test
