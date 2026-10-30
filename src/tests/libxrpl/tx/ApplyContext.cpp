#include <xrpl/tx/ApplyContext.h>

#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>

#include <cstdint>
#include <memory>

namespace xrpl::test {

/**
 * A funded alice over an open ledger, plus a signed transaction to build an
 * ApplyContext around.
 */
class ApplyContextTests : public ::testing::Test
{
protected:
    TxTest env_;
    Account const alice_{"alice"};
    std::shared_ptr<STTx const> tx_;

    ApplyContextTests()
    {
        env_.createAccount(alice_, XRP(10'000));
        transactions::AccountSetBuilder builder{alice_.id()};
        builder.setSequence(env_.getAccountRoot(alice_.id()).getSequence());
        builder.setFee(XRPAmount(10));
        tx_ = builder.build(alice_.pk(), alice_.sk()).getSTTx();
    }

    /**
     * Build a ticket entry that is not in the ledger.
     *
     * @param seq The raw ticket sequence.
     * @return The new entry.
     */
    SLE::pointer
    makeTicket(std::uint32_t seq) const
    {
        auto sle = std::make_shared<SLE>(keylet::ticket(alice_.id(), SeqProxy::rawTicket(seq)));
        sle->setAccountID(sfAccount, alice_.id());
        sle->setFieldU32(sfTicketSequence, seq);
        sle->setFieldU64(sfOwnerNode, 0);
        return sle;
    }
};

TEST_F(ApplyContextTests, discard_drops_pending_state_and_keeps_the_context_usable)
{
    ASSERT_NE(tx_, nullptr);
    // A closed base makes apply() produce metadata, which an open ledger
    // does not.
    OpenView open(&env_.getClosedLedger());
    ApplyContext ctx(env_.getServiceRegistry(), open, *tx_, tesSUCCESS, XRPAmount{10}, TapRetry);

    // Buffer an inserted entry, a modified entry, destroyed drops and a
    // delivered amount through the context's own surface.
    auto const stale = makeTicket(1);
    ctx.view().insert(stale);
    auto const account = ctx.view().peek(keylet::account(alice_.id()));
    ASSERT_NE(account, nullptr);
    auto const baseSeq = account->getFieldU32(sfSequence);
    account->setFieldU32(sfSequence, baseSeq + 1);
    ctx.view().update(account);
    ctx.destroyXRP(XRPAmount{10});
    ctx.deliver(STAmount{XRPAmount{5}});
    ASSERT_EQ(ctx.size(), 2);

    ctx.discard();

    // Reads fall through to the open ledger again and the flags survive.
    EXPECT_EQ(ctx.size(), 0);
    EXPECT_FALSE(ctx.view().exists(keylet::unchecked(stale->key())));
    auto const reread = ctx.view().read(keylet::account(alice_.id()));
    ASSERT_NE(reread, nullptr);
    EXPECT_EQ(reread->getFieldU32(sfSequence), baseSeq);
    EXPECT_EQ(ctx.view().flags(), TapRetry);

    // Only changes made after the discard reach the target view.
    XRPAmount const keptBurn{3};
    auto const fresh = makeTicket(2);
    ctx.view().insert(fresh);
    ctx.destroyXRP(keptBurn);
    auto const meta = ctx.apply(tesSUCCESS);
    ASSERT_TRUE(meta.has_value());
    EXPECT_FALSE(meta->getDeliveredAmount().has_value());
    EXPECT_FALSE(open.exists(keylet::unchecked(stale->key())));
    EXPECT_TRUE(open.exists(keylet::unchecked(fresh->key())));
    auto const applied = open.read(keylet::account(alice_.id()));
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getFieldU32(sfSequence), baseSeq);

    // OpenView buffers the burn without touching its header, so flush into a
    // real Ledger to see it: only the post-discard burn is gone, not the
    // discarded one as well.
    Ledger const& prev = env_.getClosedLedger();
    Ledger next(prev, prev.header().closeTime + prev.header().closeTimeResolution);
    open.apply(next);
    EXPECT_EQ(next.header().drops, prev.header().drops - keptBurn);
}

}  // namespace xrpl::test
