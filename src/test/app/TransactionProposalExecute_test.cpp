#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/multisign.h>
#include <test/jtx/pay.h>
#include <test/jtx/proposal.h>
#include <test/jtx/regkey.h>
#include <test/jtx/rpc.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/apply.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace xrpl::test {

/**
 * Proof of concept for TransactionProposalExecute (On-Chain Cosigner,
 * approval model §8), with the focus on a proposed Batch.
 *
 * TransactionProposalSign does not exist yet, so each test writes approvals
 * straight into the proposal, in a copy of the open ledger, and applies the
 * Execute to that copy. A write to the real open ledger would be lost when the
 * ledger closes, because the closed ledger is rebuilt from transactions. The
 * copy is applied to with applyTransaction, the same entry point that builds a
 * closed ledger, so the proposed transaction and a proposed Batch's inner
 * transactions are applied as they would be in consensus.
 */
struct TransactionProposalExecute_test : public beast::unit_test::Suite
{
    using SlotWriter = std::function<void(STObject&)>;

    // The proposed form of a transaction: unsigned, ticket-based, Fee 0, and
    // marked with tfProposedTxn.
    static json::Value
    proposed(json::Value tx, std::uint32_t ticketSeq, bool markProposed = true)
    {
        tx[jss::Fee] = "0";
        tx[jss::SigningPubKey] = "";
        tx[jss::Sequence] = 0;
        tx[sfTicketSequence.jsonName] = ticketSeq;
        if (markProposed)
            tx[jss::Flags] = tx[jss::Flags].asUInt() | tfProposedTxn;
        return tx;
    }

    static uint256
    createProposal(jtx::Env& env, jtx::Account const& target, json::Value const& payload)
    {
        using namespace std::chrono_literals;
        auto const ticketSeq = payload[sfTicketSequence.jsonName].asUInt();
        env(jtx::proposal::create(target, payload, jtx::proposal::expiration(env, 1000s)));
        env.close();
        return keylet::txProposal(target.id(), ticketSeq).key;
    }

    // Write approvals into the proposal, as TransactionProposalSign would.
    static void
    approve(OpenView& view, uint256 const& proposalID, SlotWriter const& write)
    {
        auto const before = view.read(keylet::txProposal(proposalID));
        if (!before)
            return;
        auto after = std::make_shared<SLE>(*before);
        STObject proposedTx = after->getFieldObject(sfProposedTransaction);
        write(proposedTx);
        after->setFieldObject(sfProposedTransaction, proposedTx);
        view.rawReplace(after);
    }

    static void
    directApproval(STObject& slot, jtx::Account const& approver)
    {
        slot.setFieldVL(sfSigningPubKey, approver.pk().slice());
    }

    static STObject
    approval(SField const& name, jtx::Account const& account, jtx::Account const& keyOwner)
    {
        STObject entry = STObject::makeInnerObject(name);
        entry.setAccountID(sfAccount, account.id());
        entry.setFieldVL(sfSigningPubKey, keyOwner.pk().slice());
        entry.setFieldVL(sfTxnSignature, Slice{});
        return entry;
    }

    // Shares for the top-level slot, one per approver, sorted by account.
    static void
    shares(STObject& slot, std::vector<jtx::Account> approvers)
    {
        std::ranges::sort(approvers, {}, &jtx::Account::id);
        STArray signers(sfSigners);
        for (auto const& a : approvers)
            signers.push_back(approval(sfSigner, a, a));
        slot.setFieldArray(sfSigners, signers);
    }

    static STTx
    proposedTxn(OpenView const& view, uint256 const& proposalID)
    {
        return STTx{STObject{
            view.read(keylet::txProposal(proposalID))->getFieldObject(sfProposedTransaction)}};
    }

    static XRPAmount
    balance(OpenView const& view, jtx::Account const& account)
    {
        return view.read(keylet::account(account.id()))->getFieldAmount(sfBalance).xrp();
    }

    static std::uint32_t
    ownerCount(OpenView const& view, jtx::Account const& account)
    {
        return view.read(keylet::account(account.id()))->getFieldU32(sfOwnerCount);
    }

    // A view on the last closed ledger. Like the view BuildLedger applies a
    // transaction set to, it records metadata, which an open view does not.
    static OpenView
    closedView(jtx::Env& env)
    {
        auto const closed = env.closed();
        return OpenView(&*closed, closed);
    }

    // The result recorded in the view's metadata for each transaction.
    static std::map<uint256, TER>
    results(OpenView const& view)
    {
        std::map<uint256, TER> out;
        for (auto const& [tx, meta] : view.txs)
            out.emplace(
                tx->getTransactionID(), TER::fromInt(meta->getFieldU8(sfTransactionResult)));
        return out;
    }

    // What preflight + preclaim decide for the Execute, without applying any
    // inner transaction.
    static TER
    executeResult(
        jtx::Env& env,
        OpenView const& base,
        jtx::Account const& executor,
        uint256 const& proposalID,
        XRPAmount fee)
    {
        OpenView view(base);
        auto const jt = env.jt(jtx::proposal::execute(executor, proposalID), jtx::Fee(fee));
        return xrpl::apply(env.app(), view, *jt.stx, TapNone, env.journal).ter;
    }

    // The Execute as consensus applies it: the proposed transaction, and a
    // proposed Batch's inner transactions, are applied after it.
    static uint256
    execute(
        jtx::Env& env,
        OpenView& view,
        jtx::Account const& executor,
        uint256 const& proposalID,
        XRPAmount fee)
    {
        auto const jt = env.jt(jtx::proposal::execute(executor, proposalID), jtx::Fee(fee));
        auto const ret = applyTransaction(env.app(), view, *jt.stx, false, TapNone, env.journal);
        env.test.expect(ret == ApplyTransactionResult::Success, "execute applied");
        return jt.stx->getTransactionID();
    }

    // A proposed Payment, approved by the target's own key.
    void
    testDirectApproval(FeatureBitset features)
    {
        testcase("direct approval");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"}, dest{"dest"}, exec{"exec"};
        env.fund(XRP(10000), alice, dest, exec);
        env.close();

        auto const ticketSeq = proposal::createTicket(env, alice);
        auto const id = createProposal(env, alice, proposed(pay(alice, dest, XRP(100)), ticketSeq));

        auto const write = [&](STObject& tx) { directApproval(tx, alice); };

        // The fee is checked only in an open ledger.
        auto const base = env.current()->fees().base;
        {
            OpenView open(*env.current());
            approve(open, id, write);
            BEAST_EXPECT(executeResult(env, open, exec, id, base) == telINSUF_FEE_P);
        }

        auto view = closedView(env);
        approve(view, id, write);

        auto const aliceBefore = balance(view, alice);
        auto const destBefore = balance(view, dest);
        auto const execBefore = balance(view, exec);
        auto const innerID = proposedTxn(view, id).getTransactionID();

        // The executor pays its own base fee plus the proposed Payment's.
        auto const executeID = execute(env, view, exec, id, base * 2);

        auto const res = results(view);
        BEAST_EXPECT(res.size() == 2);
        BEAST_EXPECT(res.contains(executeID) && res.at(executeID) == tesSUCCESS);
        BEAST_EXPECT(res.contains(innerID) && res.at(innerID) == tesSUCCESS);

        // The target pays no fee; the ticket and the proposal are gone.
        BEAST_EXPECT(balance(view, alice) == aliceBefore - XRP(100));
        BEAST_EXPECT(balance(view, dest) == destBefore + XRP(100));
        BEAST_EXPECT(balance(view, exec) == execBefore - base * 2);
        BEAST_EXPECT(!view.exists(keylet::txProposal(id)));
        BEAST_EXPECT(!view.exists(keylet::ticket(alice.id(), SeqProxy::rawTicket(ticketSeq))));
        BEAST_EXPECT(ownerCount(view, alice) == 0);
    }

    // Approvals are re-checked against the current ledger.
    void
    testAuthorization(FeatureBitset features)
    {
        testcase("authorization re-check");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"}, bob{"bob"}, carol{"carol"}, dave{"dave"}, dest{"dest"},
            exec{"exec"};
        env.fund(XRP(10000), alice, bob, carol, dave, dest, exec);
        env.close();

        auto const ticketSeq = proposal::createTicket(env, alice);
        auto const id = createProposal(env, alice, proposed(pay(alice, dest, XRP(100)), ticketSeq));
        XRPAmount const fee{1000};

        auto const check = [&](SlotWriter const& write, TER expected) {
            OpenView view(*env.current());
            approve(view, id, write);
            return executeResult(env, view, exec, id, fee) == expected;
        };

        // An empty slot authorizes nothing.
        BEAST_EXPECT(check([](STObject&) {}, tefBAD_AUTH));
        // A key that is not alice's.
        BEAST_EXPECT(check([&](STObject& tx) { directApproval(tx, carol); }, tefBAD_AUTH));
        // A slot that carries signature bytes is not an approval.
        BEAST_EXPECT(check(
            [&](STObject& tx) {
                directApproval(tx, alice);
                tx.setFieldVL(sfTxnSignature, Slice{"\xAB", 1});
            },
            tefBAD_AUTH));
        BEAST_EXPECT(check([&](STObject& tx) { directApproval(tx, alice); }, tesSUCCESS));

        // Shares need the current SignerList and its quorum.
        env(signers(alice, 2, {{bob, 1}, {carol, 1}}));
        env.close();
        BEAST_EXPECT(check([&](STObject& tx) { shares(tx, {bob, carol}); }, tesSUCCESS));
        BEAST_EXPECT(check([&](STObject& tx) { shares(tx, {bob}); }, tefBAD_QUORUM));
        BEAST_EXPECT(check([&](STObject& tx) { shares(tx, {bob, dave}); }, tefBAD_SIGNATURE));

        // Key rotation: once the master key is disabled, an approval recorded
        // with it no longer counts, and one with the regular key does.
        env(regkey(alice, dave));
        env(fset(alice, asfDisableMaster), Sig(alice));
        env.close();
        BEAST_EXPECT(check([&](STObject& tx) { directApproval(tx, alice); }, tefMASTER_DISABLED));
        BEAST_EXPECT(check([&](STObject& tx) { directApproval(tx, dave); }, tesSUCCESS));
    }

    // Proposal-level failures, and the flag that keeps a proposed transaction
    // off every other path.
    void
    testProposalChecks(FeatureBitset features)
    {
        testcase("proposal checks");
        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};
        Account const alice{"alice"}, dest{"dest"}, exec{"exec"};
        env.fund(XRP(10000), alice, dest, exec);
        env.close();

        XRPAmount const fee{1000};
        OpenView const empty(*env.current());
        BEAST_EXPECT(executeResult(env, empty, exec, uint256{1}, fee) == tecNO_ENTRY);

        // A payload not marked tfProposedTxn cannot be executed.
        {
            auto const ticketSeq = proposal::createTicket(env, alice);
            auto const id = createProposal(
                env, alice, proposed(pay(alice, dest, XRP(1)), ticketSeq, /*markProposed=*/false));
            OpenView view(*env.current());
            approve(view, id, [&](STObject& tx) { directApproval(tx, alice); });
            BEAST_EXPECT(executeResult(env, view, exec, id, fee) == tecNO_PERMISSION);
        }

        // A terminal proposal cannot be executed.
        {
            auto const ticketSeq = proposal::createTicket(env, alice);
            auto const payload = proposed(pay(alice, dest, XRP(1)), ticketSeq);
            env(proposal::create(alice, payload, proposal::expiration(env, 10s)));
            env.close();
            env.close(20s);
            auto const id = keylet::txProposal(alice.id(), ticketSeq).key;
            OpenView view(*env.current());
            approve(view, id, [&](STObject& tx) { directApproval(tx, alice); });
            BEAST_EXPECT(executeResult(env, view, exec, id, fee) == tecEXPIRED);
        }

        // The proposed transaction itself cannot be submitted.
        {
            auto const ticketSeq = proposal::createTicket(env, alice);
            env(proposed(pay(alice, dest, XRP(1)), ticketSeq),
                Rpc("invalidTransaction",
                    "fails local checks: Proposed transactions are never considered validly "
                    "signed."));
        }
    }

    // The proposed Batch: alice's outer Batch with an inner from alice and an
    // inner from bob. alice approves the top-level slot and bob his
    // BatchSigners entry.
    struct BatchFixture
    {
        jtx::Account alice{"alice"}, bob{"bob"}, carol{"carol"}, dest{"dest"}, exec{"exec"};
        std::uint32_t ticketSeq = 0;
        uint256 id;
    };

    static BatchFixture
    makeBatch(jtx::Env& env, std::uint32_t mode, STAmount const& bobPays)
    {
        using namespace jtx;
        BatchFixture f;
        env.fund(XRP(10000), f.alice, f.bob, f.carol, f.dest, f.exec);
        env.close();

        f.ticketSeq = proposal::createTicket(env, f.alice);

        // alice's Create spends one sequence before the Batch runs.
        json::Value outer = batch::outer(f.alice, 0, XRPAmount{0}, mode);
        auto& raw = outer[jss::RawTransactions];
        raw[0u][jss::RawTransaction] =
            proposal::innerTx(pay(f.alice, f.dest, XRP(100)), env.seq(f.alice) + 1);
        raw[1u][jss::RawTransaction] =
            proposal::innerTx(pay(f.bob, f.dest, bobPays), env.seq(f.bob));

        f.id = createProposal(env, f.alice, proposed(outer, f.ticketSeq));
        return f;
    }

    static void
    batchSigners(STObject& tx, std::vector<STObject> const& entries)
    {
        STArray signers(sfBatchSigners);
        for (auto const& e : entries)
            signers.push_back(e);
        tx.setFieldArray(sfBatchSigners, signers);
    }

    void
    testBatch(FeatureBitset features)
    {
        testcase("proposed batch");
        using namespace jtx;

        Env env{*this, features};
        auto const f = makeBatch(env, tfAllOrNothing, XRP(50));

        auto const write = [&](STObject& tx) {
            directApproval(tx, f.alice);
            batchSigners(tx, {approval(sfBatchSigner, f.bob, f.bob)});
        };

        // Execute's base fee plus the Batch's: its own two, one per inner,
        // and one for bob's BatchSigners entry. Checked in an open ledger.
        auto const base = env.current()->fees().base;
        XRPAmount const fee = base + base * 5;
        {
            OpenView open(*env.current());
            approve(open, f.id, write);
            BEAST_EXPECT(executeResult(env, open, f.exec, f.id, fee - 1) == telINSUF_FEE_P);
            BEAST_EXPECT(executeResult(env, open, f.exec, f.id, fee) == tesSUCCESS);
        }

        auto view = closedView(env);
        approve(view, f.id, write);

        auto const batchTx = proposedTxn(view, f.id);
        auto const innerIDs = batchTx.getBatchTransactionIDs();
        auto const aliceBefore = balance(view, f.alice);
        auto const bobBefore = balance(view, f.bob);
        auto const destBefore = balance(view, f.dest);
        auto const execBefore = balance(view, f.exec);

        auto const executeID = execute(env, view, f.exec, f.id, fee);

        // Execute -> Batch -> inners, each with its own result.
        auto const res = results(view);
        BEAST_EXPECT(res.size() == 4);
        for (auto const& txID : {executeID, batchTx.getTransactionID(), innerIDs[0], innerIDs[1]})
            BEAST_EXPECT(res.contains(txID) && res.at(txID) == tesSUCCESS);

        BEAST_EXPECT(balance(view, f.alice) == aliceBefore - XRP(100));
        BEAST_EXPECT(balance(view, f.bob) == bobBefore - XRP(50));
        BEAST_EXPECT(balance(view, f.dest) == destBefore + XRP(150));
        BEAST_EXPECT(balance(view, f.exec) == execBefore - fee);
        BEAST_EXPECT(!view.exists(keylet::txProposal(f.id)));
        BEAST_EXPECT(ownerCount(view, f.alice) == 0);
    }

    // Each participant's approval is re-checked, and every participant needs
    // one.
    void
    testBatchAuthorization(FeatureBitset features)
    {
        testcase("proposed batch authorization");
        using namespace jtx;

        Env env{*this, features};
        auto const f = makeBatch(env, tfAllOrNothing, XRP(50));
        XRPAmount const fee{1000};

        auto const check = [&](SlotWriter const& write, TER expected) {
            OpenView view(*env.current());
            approve(view, f.id, write);
            return executeResult(env, view, f.exec, f.id, fee) == expected;
        };

        // bob has no BatchSigners entry.
        BEAST_EXPECT(check([&](STObject& tx) { directApproval(tx, f.alice); }, tefBAD_AUTH));
        // alice's slot is empty.
        BEAST_EXPECT(check(
            [&](STObject& tx) { batchSigners(tx, {approval(sfBatchSigner, f.bob, f.bob)}); },
            tefBAD_AUTH));
        // bob's entry holds carol's key.
        BEAST_EXPECT(check(
            [&](STObject& tx) {
                directApproval(tx, f.alice);
                batchSigners(tx, {approval(sfBatchSigner, f.bob, f.carol)});
            },
            tefBAD_AUTH));
        // bob's entry carries signature bytes.
        BEAST_EXPECT(check(
            [&](STObject& tx) {
                directApproval(tx, f.alice);
                auto entry = approval(sfBatchSigner, f.bob, f.bob);
                entry.setFieldVL(sfTxnSignature, Slice{"\xAB", 1});
                batchSigners(tx, {entry});
            },
            tefBAD_AUTH));
        // An extra entry for an account with no inner transaction.
        BEAST_EXPECT(check(
            [&](STObject& tx) {
                directApproval(tx, f.alice);
                std::vector<STObject> entries{
                    approval(sfBatchSigner, f.bob, f.bob),
                    approval(sfBatchSigner, f.carol, f.carol)};
                std::ranges::sort(
                    entries, {}, [](STObject const& e) { return e.getAccountID(sfAccount); });
                batchSigners(tx, entries);
            },
            tefBAD_AUTH));

        // bob's approval stops counting once his master key is disabled.
        env(regkey(f.bob, f.carol));
        env(fset(f.bob, asfDisableMaster), Sig(f.bob));
        env.close();
        BEAST_EXPECT(check(
            [&](STObject& tx) {
                directApproval(tx, f.alice);
                batchSigners(tx, {approval(sfBatchSigner, f.bob, f.bob)});
            },
            tefMASTER_DISABLED));
        BEAST_EXPECT(check(
            [&](STObject& tx) {
                directApproval(tx, f.alice);
                batchSigners(tx, {approval(sfBatchSigner, f.bob, f.carol)});
            },
            tesSUCCESS));
    }

    // An inner failure in an all-or-nothing Batch rolls back the inners, but
    // the Execute and the Batch still apply, spend the ticket, and delete the
    // proposal (approval model §8.4, as for Batch).
    void
    testBatchInnerFailure(FeatureBitset features)
    {
        testcase("proposed batch inner failure");
        using namespace jtx;

        Env env{*this, features};
        auto const f = makeBatch(env, tfAllOrNothing, XRP(1'000'000));

        auto view = closedView(env);
        approve(view, f.id, [&](STObject& tx) {
            directApproval(tx, f.alice);
            batchSigners(tx, {approval(sfBatchSigner, f.bob, f.bob)});
        });

        auto const batchID = proposedTxn(view, f.id).getTransactionID();
        auto const destBefore = balance(view, f.dest);
        auto const executeID = execute(env, view, f.exec, f.id, XRPAmount{1000});

        auto const res = results(view);
        BEAST_EXPECT(res.size() == 2);
        BEAST_EXPECT(res.contains(executeID) && res.at(executeID) == tesSUCCESS);
        BEAST_EXPECT(res.contains(batchID) && res.at(batchID) == tesSUCCESS);
        BEAST_EXPECT(balance(view, f.dest) == destBefore);
        BEAST_EXPECT(!view.exists(keylet::txProposal(f.id)));
        BEAST_EXPECT(!view.exists(keylet::ticket(f.alice.id(), SeqProxy::rawTicket(f.ticketSeq))));
    }

    void
    run() override
    {
        FeatureBitset const all{jtx::testableAmendments()};
        testDirectApproval(all);
        testAuthorization(all);
        testProposalChecks(all);
        testBatch(all);
        testBatchAuthorization(all);
        testBatchInnerFailure(all);
    }
};

BEAST_DEFINE_TESTSUITE(TransactionProposalExecute, app, xrpl);

}  // namespace xrpl::test
