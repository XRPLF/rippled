#include <xrpl/ledger/ApplyViewImpl.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace xrpl::test {

namespace {

/**
 * Target that records what an OpenView hands down when it is applied, so a
 * test can see which entries an ApplyViewImpl changed, how, and the drops it
 * destroyed, without reaching into private state.
 */
class RecordingRawView final : public TxsRawView
{
public:
    enum class Action { Erase, Insert, Replace };

    XRPAmount destroyed{0};
    std::vector<std::pair<Action, UInt256>> entries;

    /**
     * Whether an entry with @p key was recorded with @p action.
     *
     * @param action The raw operation to look for.
     * @param key The entry's key.
     * @return True if that exact operation on that key was recorded.
     */
    [[nodiscard]] bool
    recorded(Action action, UInt256 const& key) const
    {
        return std::ranges::find(entries, std::pair{action, key}) != entries.end();
    }

    /**
     * Whether any operation on @p key was recorded.
     *
     * @param key The entry's key.
     * @return True if the key appears with any action.
     */
    [[nodiscard]] bool
    touched(UInt256 const& key) const
    {
        return std::ranges::any_of(entries, [&](auto const& e) { return e.second == key; });
    }

    void
    rawErase(SLE::Ref sle) override
    {
        entries.emplace_back(Action::Erase, sle->key());
    }

    void
    rawInsert(SLE::Ref sle) override
    {
        entries.emplace_back(Action::Insert, sle->key());
    }

    void
    rawReplace(SLE::Ref sle) override
    {
        entries.emplace_back(Action::Replace, sle->key());
    }

    void
    rawDestroyXRP(XRPAmount const& fee) override
    {
        destroyed += fee;
    }

    void
    rawTxInsert(
        ReadView::key_type const&,
        std::shared_ptr<Serializer const> const&,
        std::shared_ptr<Serializer const> const&) override
    {
    }
};

}  // namespace

/**
 * A funded alice and bob over a closed ledger, plus the pieces needed to
 * drive ApplyViewImpl::apply(): a signed transaction and a null journal.
 */
class ApplyViewImplTests : public ::testing::Test
{
protected:
    TxTest env_;
    Account const alice_{"alice"};
    Account const bob_{"bob"};
    beast::Journal const j_{beast::Journal::getNullSink()};

    ApplyViewImplTests()
    {
        env_.createAccount(alice_, XRP(10'000));
        env_.createAccount(bob_, XRP(10'000));
    }

    /**
     * Build a signed AccountSet from alice, good enough for metadata.
     *
     * @return The transaction.
     */
    std::shared_ptr<STTx const>
    makeTx()
    {
        transactions::AccountSetBuilder builder{alice_.id()};
        builder.setSequence(env_.getAccountRoot(alice_.id()).getSequence());
        builder.setFee(XRPAmount(10));
        return builder.build(alice_.pk(), alice_.sk()).getSTTx();
    }

    /**
     * Build a ticket entry that is not in the closed ledger.
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

    /**
     * Apply the view to a fresh OpenView over the closed ledger and then
     * flush that OpenView into a recorder.
     *
     * @param av The view to apply.
     * @param rec Receives what the OpenView applied.
     * @return The metadata produced by the view.
     */
    std::optional<TxMeta>
    applyAndRecord(ApplyViewImpl& av, RecordingRawView& rec)
    {
        auto const tx = makeTx();
        if (!tx)
        {
            ADD_FAILURE() << "tx build failed";
            return std::nullopt;
        }
        OpenView open(&env_.getClosedLedger());
        auto meta = av.apply(open, *tx, tesSUCCESS, std::nullopt, false, j_);
        open.apply(rec);
        return meta;
    }
};

TEST_F(ApplyViewImplTests, reset_drops_pending_state)
{
    ReadView const& base = env_.getClosedLedger();
    ApplyViewImpl av(&base, TapRetry);

    // Touch every kind of state the view buffers: an inserted entry, a
    // modified entry, an erased entry, destroyed drops and a delivered amount.
    auto const ticket = makeTicket(1);
    av.rawInsert(ticket);
    auto const account = av.peek(keylet::account(alice_.id()));
    ASSERT_NE(account, nullptr);
    auto const baseSeq = account->getFieldU32(sfSequence);
    account->setFieldU32(sfSequence, baseSeq + 1);
    av.update(account);
    auto const bob = av.peek(keylet::account(bob_.id()));
    ASSERT_NE(bob, nullptr);
    av.erase(bob);
    av.rawDestroyXRP(XRPAmount{10});
    av.deliver(STAmount{XRPAmount{5}});

    ASSERT_EQ(av.size(), 3);
    ASSERT_TRUE(av.exists(keylet::unchecked(ticket->key())));
    ASSERT_FALSE(av.exists(keylet::account(bob_.id())));

    av.reset();

    // Buffered changes are gone and reads fall through to the base again.
    EXPECT_EQ(av.size(), 0);
    EXPECT_FALSE(av.exists(keylet::unchecked(ticket->key())));
    EXPECT_TRUE(av.exists(keylet::account(bob_.id())));
    auto const reread = av.read(keylet::account(alice_.id()));
    ASSERT_NE(reread, nullptr);
    EXPECT_EQ(reread->getFieldU32(sfSequence), baseSeq);

    // Base and flags are untouched.
    EXPECT_EQ(av.flags(), TapRetry);
    EXPECT_EQ(av.open(), base.open());
    EXPECT_EQ(&av.header(), &base.header());
    EXPECT_EQ(&av.fees(), &base.fees());

    // Applying the reset view produces empty metadata, no delivered amount
    // and no destroyed drops.
    RecordingRawView rec;
    auto const meta = applyAndRecord(av, rec);
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->getNodes().size(), 0);
    EXPECT_FALSE(meta->getDeliveredAmount().has_value());
    EXPECT_TRUE(rec.entries.empty());
    EXPECT_EQ(rec.destroyed, XRPAmount{0});
}

TEST_F(ApplyViewImplTests, reset_after_apply_restores_a_clean_view)
{
    ApplyViewImpl av(&env_.getClosedLedger(), TapNone);

    auto const ticket = makeTicket(1);
    av.rawInsert(ticket);
    av.rawDestroyXRP(XRPAmount{10});
    STAmount const delivered{XRPAmount{5}};
    av.deliver(delivered);

    // apply() forwards the buffered changes but does not consume them.
    RecordingRawView first;
    auto const applied = applyAndRecord(av, first);
    ASSERT_TRUE(applied.has_value());
    ASSERT_TRUE(applied->getDeliveredAmount().has_value());
    EXPECT_EQ(*applied->getDeliveredAmount(), delivered);
    EXPECT_TRUE(first.recorded(RecordingRawView::Action::Insert, ticket->key()));
    EXPECT_EQ(first.destroyed, XRPAmount{10});
    EXPECT_EQ(av.size(), 1);

    // reset() is the one operation besides destruction that is valid after
    // apply(), and it leaves nothing behind for a second apply().
    av.reset();
    EXPECT_EQ(av.size(), 0);
    EXPECT_FALSE(av.exists(keylet::unchecked(ticket->key())));

    RecordingRawView second;
    auto const meta = applyAndRecord(av, second);
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->getNodes().size(), 0);
    EXPECT_FALSE(meta->getDeliveredAmount().has_value());
    EXPECT_TRUE(second.entries.empty());
    EXPECT_EQ(second.destroyed, XRPAmount{0});
}

TEST_F(ApplyViewImplTests, usable_again_after_reset)
{
    ApplyViewImpl av(&env_.getClosedLedger(), TapNone);

    auto const stale = makeTicket(1);
    av.rawInsert(stale);
    av.rawDestroyXRP(XRPAmount{10});
    av.deliver(STAmount{XRPAmount{5}});
    av.reset();

    // Only what happens after the reset is visible.
    auto const fresh = makeTicket(2);
    av.rawInsert(fresh);
    av.rawDestroyXRP(XRPAmount{3});
    STAmount const delivered{XRPAmount{7}};
    av.deliver(delivered);

    EXPECT_EQ(av.size(), 1);
    EXPECT_FALSE(av.exists(keylet::unchecked(stale->key())));
    EXPECT_TRUE(av.exists(keylet::unchecked(fresh->key())));

    std::vector<UInt256> visited;
    OpenView open(&env_.getClosedLedger());
    av.visit(open, [&](UInt256 const& key, bool, SLE::ConstRef, SLE::ConstRef) {
        visited.push_back(key);
    });
    EXPECT_EQ(visited, std::vector<UInt256>{fresh->key()});

    RecordingRawView rec;
    auto const meta = applyAndRecord(av, rec);
    ASSERT_TRUE(meta.has_value());
    ASSERT_TRUE(meta->getDeliveredAmount().has_value());
    EXPECT_EQ(*meta->getDeliveredAmount(), delivered);
    EXPECT_EQ(rec.destroyed, XRPAmount{3});
    EXPECT_TRUE(rec.recorded(RecordingRawView::Action::Insert, fresh->key()));
    EXPECT_FALSE(rec.touched(stale->key()));
}

}  // namespace xrpl::test
