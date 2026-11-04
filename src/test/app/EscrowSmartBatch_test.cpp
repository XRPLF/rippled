#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/escrow.h>
#include <test/jtx/fee.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace xrpl::test {

// Smart Escrow transactions (an EscrowCreate carrying Bytecode, an EscrowFinish carrying Gas)
// used as Batch inners. Only as inners: the outer is always a plain Batch, whose format has no
// Gas or Bytecode field, and a Batch cannot nest.
//
// The tests, simplest first:
// - the SmartEscrow amendment still gates them inside a Batch;
// - one batch can create a smart escrow and finish it;
// - an inner finish runs its contract, and the batch mode acts on the answer;
// - the outer fee covers the inners' Gas and Bytecode charges.
//
// Alice submits every batch and owns every escrow; bob receives. All inners are alice's, so
// no batch signers are needed.
//
// These are Beast tests rather than gtest because gtest's TxTest never applies a Batch's
// inners. Beast tests cannot assemble wasm text, so the contracts are hand-written bytes.
class EscrowSmartBatch_test : public beast::unit_test::Suite
{
    // What the contract returns: 1 approves the finish, 0 rejects it. Each is written as the
    // single byte that follows the `i32.const` instruction (a signed LEB128 number).
    static constexpr std::string_view kApprove{"01"};
    static constexpr std::string_view kReject{"00"};

    // The Gas each inner finish asks for. These contracts run a couple of instructions, so
    // this is plenty. The fee test asks for the ledger's limit instead.
    static constexpr std::uint32_t kGas = 10'000;

    // `(module (memory (export "memory") 1)
    //    (func (export "escrow_finish") (result i32) (i32.const <ret>)))`
    // Imports nothing, so it runs without touching the ledger: whatever the batch does, the
    // contract's answer is fixed.
    static std::string
    contract(std::string_view ret)
    {
        std::string wasm{
            "0061736D01000000"                  // magic, version
            "0105016000017F"                    // type: () -> i32
            "03020100"                          // function: one, of that type
            "0503010001"                        // memory: one page
            "071A02"                            // export: two
            "066D656D6F72790200"                // "memory"
            "0D657363726F775F66696E6973680000"  // "escrow_finish"
            "0A06010400"                        // code: one body, no locals
            "41"};                              // i32.const
        wasm += ret;                            // <ret>
        wasm += "0B";                           // end
        return wasm;
    }

    // What Gas adds to an EscrowFinish's fee: the allowance priced at `gasPrice`, rounded up
    // (`EscrowFinish::calculateBaseFee`).
    static XRPAmount
    gasFee(jtx::Env const& env, std::uint32_t gas)
    {
        auto const gasPrice = env.current()->fees().gasPrice;
        return XRPAmount{static_cast<XRPAmount::value_type>(
            ((std::uint64_t{gas} * gasPrice) / microDropsPerDrop) + 1)};
    }

    // What Bytecode adds to an EscrowCreate's fee: nine more base fees (ten in all instead of
    // one) plus five drops a byte (`EscrowCreate::calculateBaseFee`).
    static XRPAmount
    bytecodeFee(jtx::Env const& env, std::string const& bytecode)
    {
        auto const base = env.current()->fees().base;
        return (base * 9) + XRPAmount{static_cast<XRPAmount::value_type>(bytecode.size() / 2) * 5};
    }

    // The escrow `owner` created with sequence `seq`.
    static Keylet
    escrowKey(jtx::Account const& owner, std::uint32_t seq)
    {
        return keylet::escrow(owner, SeqProxy::rawSequence(seq));
    }

    // An EscrowCreate of 100 XRP from `from` to `to`. It has no FinishAfter or Condition, so
    // only `bytecode` can release it. A smart escrow must have a CancelAfter; 1000 seconds
    // keeps it open for the whole test.
    static json::Value
    createWithBytecode(
        jtx::Env& env,
        jtx::Account const& from,
        jtx::Account const& to,
        std::string const& bytecode)
    {
        using namespace std::chrono_literals;

        auto jv = jtx::escrow::create(from, to, jtx::XRP(100));
        jv[sfBytecode.jsonName] = bytecode;
        jv[sfCancelAfter.jsonName] =
            static_cast<std::uint32_t>((env.now() + 1000s).time_since_epoch().count());
        return jv;
    }

    // An EscrowFinish of `owner`'s escrow `escrowSeq`, submitted by `owner` itself.
    static json::Value
    finishWithGas(jtx::Account const& owner, std::uint32_t escrowSeq, std::uint32_t gas)
    {
        auto jv = jtx::escrow::finish(owner, owner, escrowSeq);
        jv[sfGas.jsonName] = gas;
        return jv;
    }

    // Creates, outside any batch and paying the exact fee, a smart escrow of 100 XRP from
    // `from` to `to`. Returns its sequence.
    static std::uint32_t
    createEscrow(
        jtx::Env& env,
        jtx::Account const& from,
        jtx::Account const& to,
        std::string const& bytecode)
    {
        using namespace jtx;

        auto const seq = env.seq(from);
        env(createWithBytecode(env, from, to, bytecode),
            Fee(env.current()->fees().base + bytecodeFee(env, bytecode)));
        env.close();
        return seq;
    }

    // The inner's own metadata, in the last closed ledger, shows the contract ran: what it
    // returned, and a gas figure within the allowance.
    void
    expectRan(jtx::Env& env, uint256 const& innerID, std::uint32_t gas, std::int32_t returned)
    {
        auto const meta = env.closed()->txRead(innerID).second;
        if (!BEAST_EXPECT(meta))
            return;

        BEAST_EXPECT(meta->isFieldPresent(sfGasUsed) && meta->getFieldU32(sfGasUsed) <= gas);
        BEAST_EXPECT(
            meta->isFieldPresent(sfVMReturnCode) && meta->getFieldI32(sfVMReturnCode) == returned);
    }

    void
    testAmendmentGate()
    {
        // Without the SmartEscrow amendment, an EscrowCreate carrying Bytecode or an
        // EscrowFinish carrying Gas is refused in preflight with temDISABLED. Putting it inside
        // a Batch must not get it through. Batch's preflight runs each inner's own preflight,
        // so the inner is refused there and the whole Batch fails with temINVALID_INNER_BATCH.
        // That is a tem: nothing is applied and no fee is charged.
        //
        // With the amendment on, the same inners pass preflight (the later tests submit them),
        // so the amendment is the only reason for the refusal.
        testcase("inner smart escrow needs the amendment");

        using namespace jtx;

        Env env{*this, testableAmendments() - featureSmartEscrow};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const bytecode = contract(kApprove);

        // Each batch pays what it would owe with the amendment on, so the fee is not why it is
        // refused.
        auto const createBatchFee = batch::calcBatchFee(env, 0, 2) + bytecodeFee(env, bytecode);
        auto const finishBatchFee = batch::calcBatchFee(env, 0, 2) + gasFee(env, kGas);

        auto const preAlice = env.balance(alice);
        // Neither batch is applied, so both use the same sequence.
        auto const seq = env.seq(alice);

        // An inner EscrowCreate carrying Bytecode. The payment is there only because a Batch
        // needs at least two inners.
        env(batch::outer(alice, seq, createBatchFee, tfAllOrNothing),
            batch::Inner(createWithBytecode(env, alice, bob, bytecode), seq + 1),
            batch::Inner(pay(alice, bob, XRP(1)), seq + 2),
            Ter(temINVALID_INNER_BATCH));

        // An inner EscrowFinish carrying Gas. No smart escrow can exist without the amendment,
        // so it names an escrow that does not exist; preflight never looks the escrow up.
        env(batch::outer(alice, seq, finishBatchFee, tfAllOrNothing),
            batch::Inner(finishWithGas(alice, seq, kGas), seq + 1),
            batch::Inner(pay(alice, bob, XRP(1)), seq + 2),
            Ter(temINVALID_INNER_BATCH));
        env.close();

        // Nothing applied, nothing charged.
        BEAST_EXPECT(env.seq(alice) == seq);
        BEAST_EXPECT(env.balance(alice) == preAlice);
    }

    void
    testCreateAndFinishInOneBatch()
    {
        // One batch, two inners: the first creates a smart escrow from alice to bob, and the
        // second finishes it. Each inner is applied on top of the ones before it, so the finish
        // finds the escrow the create just made and runs its contract, which approves.
        //
        // Expected: both inners succeed and bob receives the 100 XRP. Nothing is left behind:
        // no escrow, and alice's owner count, which the bytecode raised, is back where it was.
        // Alice pays exactly the outer fee, which covers both inners' extra charges.
        testcase("create and finish in one batch");

        using namespace jtx;

        Env env{*this};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const bytecode = contract(kApprove);
        // The plain fee for a two-inner batch, plus the create's Bytecode charge, plus the
        // finish's Gas charge.
        auto const batchFee =
            batch::calcBatchFee(env, 0, 2) + bytecodeFee(env, bytecode) + gasFee(env, kGas);

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        auto const preOwners = env.ownerCount(alice);

        // The outer uses alice's next sequence and the inners the two after it. An escrow is
        // keyed by the sequence of the transaction that created it, so the finish names the
        // create's. All-or-nothing: the escrow is only worth creating if it is also finished.
        auto const seq = env.seq(alice);
        auto const escrowSeq = seq + 1;
        auto const jt = env.jt(
            batch::outer(alice, seq, batchFee, tfAllOrNothing),
            batch::Inner(createWithBytecode(env, alice, bob, bytecode), escrowSeq),
            batch::Inner(finishWithGas(alice, escrowSeq, kGas), seq + 2));
        env(jt, Ter(tesSUCCESS));
        env.close();

        // Both inners are in the ledger with tesSUCCESS, and the finish's metadata shows the
        // contract ran and returned 1 (approve).
        auto const batchID = strHex(jt.stx->getTransactionID());
        auto const& ids = jt.stx->getBatchTransactionIDs();
        if (!BEAST_EXPECT(ids.size() == 2))
            return;
        auto const& createID = ids[0];
        auto const& finishID = ids[1];
        batch::validateInnerTxn(env, batchID, strHex(createID), "EscrowCreate", "tesSUCCESS");
        batch::validateInnerTxn(env, batchID, strHex(finishID), "EscrowFinish", "tesSUCCESS");
        expectRan(env, finishID, kGas, 1);

        // The escrow came and went within the batch.
        BEAST_EXPECT(!env.le(escrowKey(alice, escrowSeq)));
        BEAST_EXPECT(env.ownerCount(alice) == preOwners);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(100));
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(100) - batchFee);
    }

    void
    testInnerFinishRunsContract()
    {
        // An inner EscrowFinish runs its escrow's contract, and the contract's answer becomes
        // that inner's result. The batch mode then acts on it like on any other inner result.
        // Each case batches the finish with a 1 XRP payment to bob; what happens to the
        // payment shows what the mode did.
        //
        // - Approve, all-or-nothing: both inners succeed and the escrow is released.
        // - Reject, independent: the finish fails with tecBYTECODE_REJECTED, the payment still
        //   goes through, and the escrow stays.
        // - Reject, all-or-nothing: the failed finish rolls back the whole batch. Neither inner
        //   reaches the ledger, the escrow stays, and alice pays only the outer fee.
        //
        // Other mode and outcome combinations are not repeated here: Batch acts only on an
        // inner's result code, and how each mode handles success and tec failures is covered in
        // Batch_test.
        testcase("inner finish runs the contract");

        using namespace jtx;

        Env env{*this};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        // The plain fee for a two-inner batch, plus the finish's Gas charge.
        auto const batchFee = batch::calcBatchFee(env, 0, 2) + gasFee(env, kGas);

        // Submits [finish the escrow, pay bob 1 XRP] in the given mode and closes the ledger.
        // The outer always succeeds; the inners' fate is what each case checks.
        auto const submit = [&](std::uint32_t escrowSeq, std::uint32_t mode) {
            auto const seq = env.seq(alice);
            auto jt = env.jt(
                batch::outer(alice, seq, batchFee, mode),
                batch::Inner(finishWithGas(alice, escrowSeq, kGas), seq + 1),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 2));
            env(jt, Ter(tesSUCCESS));
            env.close();
            return jt;
        };

        // Approve, all-or-nothing. The escrow has no FinishAfter or Condition, so only its
        // contract can release it.
        {
            auto const escrowSeq = createEscrow(env, alice, bob, contract(kApprove));
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const jt = submit(escrowSeq, tfAllOrNothing);

            auto const batchID = strHex(jt.stx->getTransactionID());
            auto const& ids = jt.stx->getBatchTransactionIDs();
            if (!BEAST_EXPECT(ids.size() == 2))
                return;
            auto const& finishID = ids[0];
            auto const& payID = ids[1];
            batch::validateInnerTxn(env, batchID, strHex(finishID), "EscrowFinish", "tesSUCCESS");
            batch::validateInnerTxn(env, batchID, strHex(payID), "Payment", "tesSUCCESS");
            expectRan(env, finishID, kGas, 1);

            // Released: bob gets the escrowed 100 XRP and the 1 XRP payment.
            BEAST_EXPECT(!env.le(escrowKey(alice, escrowSeq)));
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(100) + XRP(1));
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1) - batchFee);
        }

        // Reject, independent. The rejection is recorded on the finish's own metadata, so this
        // case is where the contract's "no" is visible.
        {
            auto const escrowSeq = createEscrow(env, alice, bob, contract(kReject));
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const jt = submit(escrowSeq, tfIndependent);

            auto const batchID = strHex(jt.stx->getTransactionID());
            auto const& ids = jt.stx->getBatchTransactionIDs();
            if (!BEAST_EXPECT(ids.size() == 2))
                return;
            auto const& finishID = ids[0];
            auto const& payID = ids[1];
            batch::validateInnerTxn(
                env, batchID, strHex(finishID), "EscrowFinish", "tecBYTECODE_REJECTED");
            batch::validateInnerTxn(env, batchID, strHex(payID), "Payment", "tesSUCCESS");
            expectRan(env, finishID, kGas, 0);

            // Not released: bob gets only the payment.
            BEAST_EXPECT(env.le(escrowKey(alice, escrowSeq)));
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(1));
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1) - batchFee);
        }

        // Reject, all-or-nothing. Same contract and batch as above; only the mode differs.
        // The rolled-back inners leave no metadata, so the case above is what shows the cause.
        {
            auto const escrowSeq = createEscrow(env, alice, bob, contract(kReject));
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const seq = env.seq(alice);

            auto const jt = submit(escrowSeq, tfAllOrNothing);

            for (auto const& id : jt.stx->getBatchTransactionIDs())
                BEAST_EXPECT(!env.closed()->txExists(id));

            // Nothing moved except the outer fee, and only the outer's sequence is used.
            BEAST_EXPECT(env.le(escrowKey(alice, escrowSeq)));
            BEAST_EXPECT(env.balance(bob) == preBob);
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.seq(alice) == seq + 1);
        }
    }

    void
    testOuterFeeCoversInnerWasm()
    {
        // The outer fee must include each inner's own fee, extra charges and all: the Gas
        // charge on an EscrowFinish and the Bytecode charge on an EscrowCreate. If Batch
        // charged a flat fee per inner instead, a Batch could run contracts for a few drops.
        //
        // Each case submits the same batch twice. First one drop short of the full fee: it must
        // be refused with telINSUF_FEE_P, with nothing applied and nothing charged. A flat
        // per-inner fee would accept it. Then the full fee: it must go through, and alice is
        // charged exactly that fee.
        testcase("outer fee covers inner gas and bytecode");

        using namespace jtx;

        Env env{*this};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        // Gas: an inner EscrowFinish asking for the largest allowance the ledger permits, so
        // the Gas charge is most of the fee.
        {
            auto const escrowSeq = createEscrow(env, alice, bob, contract(kApprove));
            auto const gas = env.current()->fees().gasLimit;
            auto const plainFee = batch::calcBatchFee(env, 0, 2);
            auto const fullFee = plainFee + gasFee(env, gas);
            BEAST_EXPECT(gasFee(env, gas) > plainFee);

            // Submits [finish the escrow, pay bob 1 XRP], paying `fee`.
            auto const submit = [&](XRPAmount const& fee, TER const& ter) {
                auto const seq = env.seq(alice);
                env(batch::outer(alice, seq, fee, tfAllOrNothing),
                    batch::Inner(finishWithGas(alice, escrowSeq, gas), seq + 1),
                    batch::Inner(pay(alice, bob, XRP(1)), seq + 2),
                    Ter(ter));
                env.close();
            };

            auto const preAlice = env.balance(alice);
            auto const seq = env.seq(alice);

            // One drop short: refused. The escrow is untouched.
            submit(fullFee - XRPAmount{1}, telINSUF_FEE_P);
            BEAST_EXPECT(env.seq(alice) == seq);
            BEAST_EXPECT(env.balance(alice) == preAlice);
            BEAST_EXPECT(env.le(escrowKey(alice, escrowSeq)));

            // Full fee: the finish runs and releases the escrow.
            submit(fullFee, tesSUCCESS);
            BEAST_EXPECT(!env.le(escrowKey(alice, escrowSeq)));
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1) - fullFee);
        }

        // Bytecode: an inner EscrowCreate carrying a contract, which adds its per-byte charge.
        {
            auto const bytecode = contract(kApprove);
            auto const fullFee = batch::calcBatchFee(env, 0, 2) + bytecodeFee(env, bytecode);

            // Submits [create the escrow, pay bob 1 XRP], paying `fee`. Returns the sequence
            // the escrow gets if the batch goes through.
            auto const submit = [&](XRPAmount const& fee, TER const& ter) {
                auto const seq = env.seq(alice);
                env(batch::outer(alice, seq, fee, tfAllOrNothing),
                    batch::Inner(createWithBytecode(env, alice, bob, bytecode), seq + 1),
                    batch::Inner(pay(alice, bob, XRP(1)), seq + 2),
                    Ter(ter));
                env.close();
                return seq + 1;
            };

            auto const preAlice = env.balance(alice);
            auto const seq = env.seq(alice);

            // One drop short: refused. No escrow is created.
            auto const refusedSeq = submit(fullFee - XRPAmount{1}, telINSUF_FEE_P);
            BEAST_EXPECT(env.seq(alice) == seq);
            BEAST_EXPECT(env.balance(alice) == preAlice);
            BEAST_EXPECT(!env.le(escrowKey(alice, refusedSeq)));

            // Full fee: the escrow is created with its contract, and its 100 XRP leaves alice.
            auto const escrowSeq = submit(fullFee, tesSUCCESS);
            auto const escrow = env.le(escrowKey(alice, escrowSeq));
            BEAST_EXPECT(escrow && escrow->isFieldPresent(sfBytecode));
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(100) - XRP(1) - fullFee);
        }
    }

public:
    void
    run() override
    {
        testAmendmentGate();
        testCreateAndFinishInOneBatch();
        testInnerFinishRunsContract();
        testOuterFeeCoversInnerWasm();
    }
};

BEAST_DEFINE_TESTSUITE(EscrowSmartBatch, app, xrpl);

}  // namespace xrpl::test
