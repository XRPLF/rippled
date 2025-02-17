//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <test/jtx.h>
#include <test/jtx/utility.h>
#include <xrpld/app/tx/apply.h>
#include <xrpl/protocol/Batch.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

class Batch_test : public beast::unit_test::suite
{
    struct TestBatchData
    {
        std::string result;
        std::string txHash;
    };

    struct TestSignData
    {
        int index;
        jtx::Account account;
    };

    void
    validateBatch(
        jtx::Env& env,
        TxID const& parentBatchId,
        std::vector<TestBatchData> const& batchResults,
        int const& pbIndex = 0)
    {
        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));

        // Validate the number of transactions in the ledger
        auto const transactions =
            jrr[jss::result][jss::ledger][jss::transactions];
        BEAST_EXPECT(transactions.size() == batchResults.size() + 1 + pbIndex);

        // Validate ttBatch is correct index
        auto getTxByIndex = [](Json::Value const& jrr,
                               int const index) -> Json::Value {
            for (auto const& txn :
                 jrr[jss::result][jss::ledger][jss::transactions])
            {
                if (txn[jss::metaData][sfTransactionIndex.jsonName] == index)
                    return txn;
            }
            return {};
        };
        auto const txn = getTxByIndex(jrr, pbIndex);
        BEAST_EXPECT(txn.isMember(jss::metaData));
        Json::Value const meta = txn[jss::metaData];
        BEAST_EXPECT(txn[sfTransactionType.jsonName] == "Batch");
        BEAST_EXPECT(meta[sfTransactionResult.jsonName] == "tesSUCCESS");

        // Validate the inner transactions
        for (TestBatchData const& batchResult : batchResults)
        {
            Json::Value jsonTx;
            jsonTx[jss::binary] = false;
            jsonTx[jss::transaction] = batchResult.txHash;
            jsonTx[jss::id] = 1;
            Json::Value const jrr =
                env.rpc("tx", batchResult.txHash)[jss::result];
            BEAST_EXPECT(
                jrr[jss::meta][sfTransactionResult.jsonName] ==
                batchResult.result);
            BEAST_EXPECT(
                jrr[jss::meta][sfParentBatchID.jsonName] ==
                to_string(parentBatchId));
        }
    }

    void
    testEnable(FeatureBitset features)
    {
        testcase("enabled");

        using namespace test::jtx;
        using namespace std::literals;

        for (bool const withBatch : {true, false})
        {
            auto const amend = withBatch ? features : features - featureBatch;
            test::jtx::Env env{*this, envconfig(), amend};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            // ttBatch
            {
                auto const seq = env.seq(alice);
                auto const batchFee = batch::calcBatchFee(env, 0, 2);
                auto const txResult =
                    withBatch ? ter(tesSUCCESS) : ter(temDISABLED);
                env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                    batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                    batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                    txResult);
                env.close();
            }

            // tfInnerBatchTxn
            {
                auto const txResult =
                    withBatch ? ter(telENV_RPC_FAILED) : ter(temINVALID_FLAG);
                env(pay(alice, bob, XRP(1)),
                    txflags(tfInnerBatchTxn),
                    txResult);
                env.close();
            }

            // Transaction Types missing flag validation
            {
                auto const txResult =
                    withBatch ? ter(telENV_RPC_FAILED) : ter(tesSUCCESS);
                env(signers(alice, 2, {{bob, 1}, {carol, 1}}),
                    txflags(tfInnerBatchTxn),
                    txResult);
                env.close();
            }

            env.close();
        }
    }

    void
    testPreflight(FeatureBitset features)
    {
        testcase("preflight");

        using namespace test::jtx;
        using namespace std::literals;

        //----------------------------------------------------------------------
        // preflight

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        // temBAD_FEE: preflight1
        {
            env(batch::outer(alice, env.seq(alice), XRP(-1), tfAllOrNothing),
                ter(temBAD_FEE));
            env.close();
        }

        // temINVALID_FLAG: Batch: inner batch flag.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 0);
            env(batch::outer(alice, seq, batchFee, tfInnerBatchTxn),
                ter(telENV_RPC_FAILED));
            env.close();
        }

        // temINVALID_FLAG: Batch: invalid flags.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 0);
            env(batch::outer(alice, seq, batchFee, tfDisallowXRP),
                ter(temINVALID_FLAG));
            env.close();
        }

        // temINVALID_FLAG: Batch: too many flags.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 0);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                txflags(tfAllOrNothing | tfOnlyOne),
                ter(temINVALID_FLAG));
            env.close();
        }

        // temARRAY_EMPTY: Batch: txns array must have at least 2 entries.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 0);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                ter(temARRAY_EMPTY));
            env.close();
        }

        // temARRAY_EMPTY: Batch: txns array must have at least 2 entries.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 0);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                ter(temARRAY_EMPTY));
            env.close();
        }

        // temARRAY_TOO_LARGE: Batch: txns array exceeds 8 entries.
        // telENV_RPC_FAILED
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 9);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                batch::inner(pay(alice, bob, XRP(1)), seq + 3),
                batch::inner(pay(alice, bob, XRP(1)), seq + 4),
                batch::inner(pay(alice, bob, XRP(1)), seq + 5),
                batch::inner(pay(alice, bob, XRP(1)), seq + 6),
                batch::inner(pay(alice, bob, XRP(1)), seq + 7),
                batch::inner(pay(alice, bob, XRP(1)), seq + 8),
                batch::inner(pay(alice, bob, XRP(1)), seq + 9),
                ter(telENV_RPC_FAILED));
            env.close();
        }

        // temMALFORMED: Batch: duplicate TxID found.
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto jt = env.jtnofill(
                batch::outer(alice, env.seq(alice), batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1));

            env(jt.jv, batch::sig(bob), ter(temMALFORMED));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: batch cannot have inner batch txn.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(
                    batch::outer(alice, seq, batchFee, tfAllOrNothing), seq),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: inner txn cannot include TxnSignature.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto tx1 = pay(alice, bob, XRP(1));
            tx1[jss::TxnSignature] = "DEADBEEF";
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(tx1, seq + 1),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: inner txn must include empty
        // SigningPubKey.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto tx1 = pay(alice, bob, XRP(1));
            tx1[jss::SigningPubKey] = strHex(alice.pk());
            tx1[jss::Sequence] = seq + 1;
            tx1[jss::Fee] = "0";
            tx1[jss::Flags] = tx1[jss::Flags].asUInt() | tfInnerBatchTxn;
            auto jt = env.jtnofill(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner_nofill(tx1),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2));

            env(jt.jv, ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: inner txn cannot include Signers.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto tx1 = pay(alice, bob, XRP(1));
            tx1[sfSigners.jsonName] = Json::arrayValue;
            tx1[sfSigners.jsonName][0U][sfSigner.jsonName] = Json::objectValue;
            tx1[sfSigners.jsonName][0U][sfSigner.jsonName][sfAccount.jsonName] =
                alice.human();
            tx1[sfSigners.jsonName][0U][sfSigner.jsonName]
               [sfSigningPubKey.jsonName] = strHex(alice.pk());
            tx1[sfSigners.jsonName][0U][sfSigner.jsonName]
               [sfTxnSignature.jsonName] = "DEADBEEF";
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(tx1, seq + 1),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: inner txn preflight failed.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(acctdelete(alice, bob), seq + 1),
                batch::inner(pay(alice, bob, XRP(-1)), seq + 2),
                ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // temARRAY_TOO_LARGE: Batch: signers array exceeds 8 entries.
        // telENV_RPC_FAILED
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 9, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(alice, bob, XRP(5)), seq + 2),
                batch::sig(
                    bob,
                    carol,
                    alice,
                    bob,
                    carol,
                    alice,
                    bob,
                    carol,
                    alice,
                    alice),
                ter(telENV_RPC_FAILED));
            env.close();
        }

        // temBAD_SIGNER: Batch: signer cannot be the outer account
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 2, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::sig(alice, bob),
                ter(temBAD_SIGNER));
            env.close();
        }

        // temBAD_SIGNER: Batch: duplicate signer found
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 2, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::sig(bob, bob),
                ter(temBAD_SIGNER));
            env.close();
        }

        // temBAD_SIGNER: Batch: no account signature for inner txn.
        // Note: Extra signature by bob
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(alice, bob, XRP(5)), seq + 2),
                batch::sig(bob),
                ter(temBAD_SIGNER));
            env.close();
        }

        // temBAD_SIGNER: Batch: no account signature for inner txn.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::sig(carol),
                ter(temBAD_SIGNER));
            env.close();
        }

        // temBAD_SIGNATURE: Batch: invalid batch txn signature.
        {
            std::vector<TestSignData> const signers = {{
                {0, bob},
            }};

            auto const seq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto jt = env.jtnofill(
                batch::outer(alice, env.seq(alice), batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), bobSeq));

            for (auto const& signer : signers)
            {
                Serializer msg;
                serializeBatch(
                    msg, tfAllOrNothing, jt.stx->getBatchTransactionIDs());
                auto const sig = ripple::sign(
                    signer.account.pk(), signer.account.sk(), msg.slice());
                jt.jv[sfBatchSigners.jsonName][signer.index]
                     [sfBatchSigner.jsonName][sfAccount.jsonName] =
                    signer.account.human();
                jt.jv[sfBatchSigners.jsonName][signer.index]
                     [sfBatchSigner.jsonName][sfSigningPubKey.jsonName] =
                    strHex(alice.pk());
                jt.jv[sfBatchSigners.jsonName][signer.index]
                     [sfBatchSigner.jsonName][sfTxnSignature.jsonName] =
                    strHex(Slice{sig.data(), sig.size()});
            }

            env(jt.jv, ter(temBAD_SIGNATURE));
            env.close();
        }

        // temBAD_SIGNER: Batch: invalid batch signers.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 2, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::inner(pay(carol, alice, XRP(5)), env.seq(carol)),
                batch::sig(bob),
                ter(temBAD_SIGNER));
            env.close();
        }
    }

    void
    testPreclaim(FeatureBitset features)
    {
        testcase("preclaim");

        using namespace test::jtx;
        using namespace std::literals;

        //----------------------------------------------------------------------
        // preclaim

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const dave = Account("dave");
        auto const elsa = Account("elsa");
        auto const frank = Account("frank");
        auto const phantom = Account("phantom");
        env.memoize(phantom);

        env.fund(XRP(10000), alice, bob, carol, dave, elsa, frank);
        env.close();

        //----------------------------------------------------------------------
        // checkBatchSign.checkMultiSign

        // tefNOT_MULTI_SIGNING: SignersList not enabled
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {dave, carol}),
                ter(tefNOT_MULTI_SIGNING));
            env.close();
        }

        env(signers(alice, 2, {{bob, 1}, {carol, 1}}));
        env.close();

        env(signers(bob, 2, {{carol, 1}, {dave, 1}, {elsa, 1}}));
        env.close();

        // tefBAD_SIGNATURE: Account not in SignersList
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {carol, frank}),
                ter(tefBAD_SIGNATURE));
            env.close();
        }
        // tefBAD_SIGNATURE: Wrong publicKey type
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {carol, Account("dave", KeyType::ed25519)}),
                ter(tefBAD_SIGNATURE));
            env.close();
        }
        // tefMASTER_DISABLED: Master key disabled
        {
            env(regkey(elsa, frank));
            env(fset(elsa, asfDisableMaster), sig(elsa));
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {carol, elsa}),
                ter(tefMASTER_DISABLED));
            env.close();
        }
        // tefBAD_SIGNATURE: Signer does not exist
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {carol, phantom}),
                ter(tefBAD_SIGNATURE));
            env.close();
        }
        // tefBAD_SIGNATURE: Signer has not enabled RegularKey
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            Account const davo{"davo", KeyType::ed25519};
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {carol, Reg{dave, davo}}),
                ter(tefBAD_SIGNATURE));
            env.close();
        }
        // tefBAD_SIGNATURE: Wrong RegularKey Set
        {
            env(regkey(dave, frank));
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            Account const davo{"davo", KeyType::ed25519};
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {carol, Reg{dave, davo}}),
                ter(tefBAD_SIGNATURE));
            env.close();
        }
        // tefBAD_QUORUM
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 2, 2);
            Account const davo{"davo", KeyType::ed25519};
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {carol}),
                ter(tefBAD_QUORUM));
            env.close();
        }
        // tesSUCCESS: BatchSigners.Signers
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            Account const davo{"davo", KeyType::ed25519};
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {carol, dave}),
                ter(tesSUCCESS));
            env.close();
        }
        // tesSUCCESS: Multisign + BatchSigners.Signers
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 4, 2);
            Account const davo{"davo", KeyType::ed25519};
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {carol, dave}),
                msig(bob, carol),
                ter(tesSUCCESS));
            env.close();
        }

        //----------------------------------------------------------------------
        // checkBatchSign.checkSingleSign

        // tefBAD_AUTH: Wrong PublicKey Type
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(bob, alice, XRP(2)), env.seq(bob)),
                batch::sig(Reg{bob, Account("bob", KeyType::ed25519)}),
                ter(tefBAD_AUTH));
            env.close();
        }
        // tefBAD_AUTH: Account is not signer
        {
            auto const ledSeq = env.current()->seq();
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1000)), seq + 1),
                batch::inner(noop(bob), ledSeq),
                batch::sig(Reg{bob, carol}),
                ter(tefBAD_AUTH));
            env.close();
        }
        // tesSUCCESS: Signed With Regular Key
        {
            env(regkey(bob, carol));
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(bob, alice, XRP(2)), env.seq(bob)),
                batch::sig(Reg{bob, carol}),
                ter(tesSUCCESS));
            env.close();
        }
        // tesSUCCESS: Signed With Master Key
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(bob, alice, XRP(2)), env.seq(bob)),
                batch::sig(bob),
                ter(tesSUCCESS));
            env.close();
        }
        // tefMASTER_DISABLED: Signed With Master Key Disabled
        {
            env(regkey(bob, carol));
            env(fset(bob, asfDisableMaster), sig(bob));
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(bob, alice, XRP(2)), env.seq(bob)),
                batch::sig(bob),
                ter(tefMASTER_DISABLED));
            env.close();
        }
    }

    void
    testBadRawTxn(FeatureBitset features)
    {
        testcase("bad raw txn");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);

        // Invalid: sfTransactionType
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto tx1 = pay(alice, bob, XRP(10));
            tx1.removeMember(jss::TransactionType);
            auto jt = env.jtnofill(
                batch::outer(alice, env.seq(alice), batchFee, tfAllOrNothing),
                batch::inner(tx1, seq + 1),
                batch::inner(pay(alice, bob, XRP(10)), seq + 2));

            env(jt.jv, batch::sig(bob), ter(telENV_RPC_FAILED));
            env.close();
        }
        // Invalid: sfAccount
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto tx1 = pay(alice, bob, XRP(10));
            tx1.removeMember(jss::Account);
            auto jt = env.jtnofill(
                batch::outer(alice, env.seq(alice), batchFee, tfAllOrNothing),
                batch::inner(tx1, seq + 1),
                batch::inner(pay(alice, bob, XRP(10)), seq + 2));

            env(jt.jv, batch::sig(bob), ter(telENV_RPC_FAILED));
            env.close();
        }
        // Invalid: sfSequence
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto tx1 = pay(alice, bob, XRP(10));
            tx1.removeMember(jss::Sequence);
            auto jt = env.jtnofill(
                batch::outer(alice, env.seq(alice), batchFee, tfAllOrNothing),
                batch::inner_nofill(tx1),
                batch::inner(pay(alice, bob, XRP(10)), seq + 2));

            env(jt.jv, batch::sig(bob), ter(telENV_RPC_FAILED));
            env.close();
        }
        // Invalid: sfFee
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto tx1 = pay(alice, bob, XRP(10));
            tx1[jss::Sequence] = seq + 1;
            tx1.removeMember(jss::Fee);
            auto jt = env.jtnofill(
                batch::outer(alice, env.seq(alice), batchFee, tfAllOrNothing),
                batch::inner_nofill(tx1),
                batch::inner(pay(alice, bob, XRP(10)), seq + 2));

            env(jt.jv, batch::sig(bob), ter(telENV_RPC_FAILED));
            env.close();
        }
        // Invalid: sfSigningPubKey
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto tx1 = pay(alice, bob, XRP(10));
            tx1[jss::Sequence] = seq + 1;
            tx1[jss::Fee] = "0";
            tx1.removeMember(jss::SigningPubKey);
            auto jt = env.jtnofill(
                batch::outer(alice, env.seq(alice), batchFee, tfAllOrNothing),
                batch::inner_nofill(tx1),
                batch::inner(pay(alice, bob, XRP(10)), seq + 2));

            env(jt.jv, batch::sig(bob), ter(telENV_RPC_FAILED));
            env.close();
        }
    }

    void
    testBadSequence(FeatureBitset features)
    {
        testcase("bad sequence");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        env.fund(XRP(1000), alice, bob, gw);
        env.close();
        env.trust(USD(1000), alice, bob);
        env(pay(gw, alice, USD(100)));
        env(pay(gw, bob, USD(100)));
        env.close();

        env(noop(bob), ter(tesSUCCESS));
        env.close();

        // Invalid: Bob Sequence is a past sequence
        {
            auto const preAliceSeq = env.seq(alice);
            auto const preAlice = env.balance(alice);
            auto const preAliceUSD = env.balance(alice, USD.issue());
            auto const preBobSeq = env.seq(bob);
            auto const preBob = env.balance(bob);
            auto const preBobUSD = env.balance(bob, USD.issue());

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), preAliceSeq + 1),
                batch::inner(pay(bob, alice, XRP(5)), preBobSeq - 10),
                batch::sig(bob),
                ter(tesSUCCESS));
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {};
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice pays fee & Bob should not be affected.
            BEAST_EXPECT(env.seq(alice) == preAliceSeq + 1);
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD);
            BEAST_EXPECT(env.seq(bob) == preBobSeq);
            BEAST_EXPECT(env.balance(bob) == preBob);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD);
        }

        // Invalid: Bob Sequence is a future sequence
        {
            auto const preAliceSeq = env.seq(alice);
            auto const preAlice = env.balance(alice);
            auto const preAliceUSD = env.balance(alice, USD.issue());
            auto const preBobSeq = env.seq(bob);
            auto const preBob = env.balance(bob);
            auto const preBobUSD = env.balance(bob, USD.issue());

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), preAliceSeq + 1),
                batch::inner(pay(bob, alice, XRP(5)), preBobSeq + 10),
                batch::sig(bob),
                ter(tesSUCCESS));
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {};
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice pays fee & Bob should not be affected.
            BEAST_EXPECT(env.seq(alice) == preAliceSeq + 1);
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD);
            BEAST_EXPECT(env.seq(bob) == preBobSeq);
            BEAST_EXPECT(env.balance(bob) == preBob);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD);
        }

        // Invalid: Outer and Inner Sequence are the same
        {
            auto const preAliceSeq = env.seq(alice);
            auto const preAlice = env.balance(alice);
            auto const preAliceUSD = env.balance(alice, USD.issue());
            auto const preBobSeq = env.seq(bob);
            auto const preBob = env.balance(bob);
            auto const preBobUSD = env.balance(bob, USD.issue());

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), preAliceSeq),
                batch::inner(pay(bob, alice, XRP(5)), preBobSeq),
                batch::sig(bob),
                ter(tesSUCCESS));
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {};
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice pays fee & Bob should not be affected.
            BEAST_EXPECT(env.seq(alice) == preAliceSeq + 1);
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD);
            BEAST_EXPECT(env.seq(bob) == preBobSeq);
            BEAST_EXPECT(env.balance(bob) == preBob);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD);
        }
    }

    void
    testBadOuterFee(FeatureBitset features)
    {
        testcase("bad outer fee");

        using namespace test::jtx;
        using namespace std::literals;

        // Bad Fee Without Signer
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(1000), alice, bob);
            env.close();

            env(noop(bob), ter(tesSUCCESS));
            env.close();

            // Bad Fee: Should be batch::calcBatchFee(env, 0, 2)
            auto const batchFee = batch::calcBatchFee(env, 0, 1);
            auto const aliceSeq = env.seq(alice);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::inner(pay(alice, bob, XRP(15)), aliceSeq + 2),
                ter(telINSUF_FEE_P));
            env.close();
        }

        // Bad Fee With MultiSign
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            env(noop(bob), ter(tesSUCCESS));
            env.close();

            env(signers(alice, 2, {{bob, 1}, {carol, 1}}));
            env.close();

            // Bad Fee: Should be batch::calcBatchFee(env, 2, 2)
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const aliceSeq = env.seq(alice);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::inner(pay(alice, bob, XRP(15)), aliceSeq + 2),
                msig(bob, carol),
                ter(telINSUF_FEE_P));
            env.close();
        }

        // Bad Fee With MultiSign + BatchSigners
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            env(noop(bob), ter(tesSUCCESS));
            env.close();

            env(signers(alice, 2, {{bob, 1}, {carol, 1}}));
            env.close();

            // Bad Fee: Should be batch::calcBatchFee(env, 3, 2)
            auto const batchFee = batch::calcBatchFee(env, 2, 2);
            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::inner(pay(bob, alice, XRP(5)), bobSeq),
                batch::sig(bob),
                msig(bob, carol),
                ter(telINSUF_FEE_P));
            env.close();
        }

        // Bad Fee With MultiSign + BatchSigners.Signers
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            env(noop(bob), ter(tesSUCCESS));
            env.close();

            env(signers(alice, 2, {{bob, 1}, {carol, 1}}));
            env.close();

            env(signers(bob, 2, {{alice, 1}, {carol, 1}}));
            env.close();

            // Bad Fee: Should be batch::calcBatchFee(env, 4, 2)
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::inner(pay(bob, alice, XRP(5)), bobSeq),
                batch::msig(bob, {alice, carol}),
                msig(bob, carol),
                ter(telINSUF_FEE_P));
            env.close();
        }

        // Bad Fee With Signer
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(1000), alice, bob);
            env.close();

            env(noop(bob), ter(tesSUCCESS));
            env.close();

            // Bad Fee: Should be batch::calcBatchFee(env, 1, 2)
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::inner(pay(bob, alice, XRP(5)), bobSeq),
                batch::sig(bob),
                ter(telINSUF_FEE_P));
            env.close();
        }

        // Bad Fee Dynamic Fee Calculation
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            auto const USD = gw["USD"];

            env.fund(XRP(1000), alice, bob, gw);
            env.close();
            auto const ammCreate =
                [&alice](STAmount const& amount, STAmount const& amount2) {
                    Json::Value jv;
                    jv[jss::Account] = alice.human();
                    jv[jss::Amount] = amount.getJson(JsonOptions::none);
                    jv[jss::Amount2] = amount2.getJson(JsonOptions::none);
                    jv[jss::TradingFee] = 0;
                    jv[jss::TransactionType] = jss::AMMCreate;
                    return jv;
                };

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(ammCreate(XRP(10), USD(10)), seq + 1),
                batch::inner(pay(alice, bob, XRP(10)), seq + 2),
                ter(telINSUF_FEE_P));
            env.close();
        }
    }

    void
    testBadInnerFee(FeatureBitset features)
    {
        testcase("bad inner fee");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};
        XRPAmount const feeDrops = env.current()->fees().base;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        env.fund(XRP(1000), alice, bob, gw);
        env.close();
        env.trust(USD(1000), alice, bob);
        env(pay(gw, alice, USD(100)));
        env(pay(gw, bob, USD(100)));
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const seq = env.seq(alice);
        auto const batchFee = batch::calcBatchFee(env, 0, 2);
        auto tx1 = pay(alice, bob, XRP(1));
        tx1[jss::Fee] = to_string(feeDrops);
        tx1[jss::Sequence] = seq + 1;
        tx1[jss::SigningPubKey] = "";
        tx1[jss::Flags] = tx1[jss::Flags].asUInt() | tfInnerBatchTxn;
        env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
            batch::inner_nofill(tx1),
            batch::inner(pay(alice, bob, XRP(2)), seq + 2),
            ter(tesSUCCESS));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {};
        env.close();
        validateBatch(env, parentBatchId, testCases);

        // Alice pays fee and sequence; Bob should not be affected.
        BEAST_EXPECT(env.seq(alice) == seq + 1);
        BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob);
    }

    void
    testCalculateBaseFee(FeatureBitset features)
    {
        testcase("calculate base fee");

        using namespace test::jtx;
        using namespace std::literals;

        // telENV_RPC_FAILED: Batch: txns array exceeds 8 entries.
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const batchFee = batch::calcBatchFee(env, 0, 9);
            auto const aliceSeq = env.seq(alice);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                ter(telENV_RPC_FAILED));
            env.close();
        }

        // temARRAY_TOO_LARGE: Batch: txns array exceeds 8 entries.
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const batchFee = batch::calcBatchFee(env, 0, 9);
            auto const aliceSeq = env.seq(alice);
            auto jt = env.jtnofill(
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq));

            env.app().openLedger().modify(
                [&](OpenView& view, beast::Journal j) {
                    auto const result =
                        ripple::apply(env.app(), view, *jt.stx, tapNONE, j);
                    BEAST_EXPECT(
                        !result.applied && result.ter == temARRAY_TOO_LARGE);
                    return result.applied;
                });
        }

        // telENV_RPC_FAILED: Batch: signers array exceeds 8 entries.
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 9, 2);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::inner(pay(alice, bob, XRP(5)), aliceSeq + 2),
                batch::sig(bob, bob, bob, bob, bob, bob, bob, bob, bob, bob),
                ter(telENV_RPC_FAILED));
            env.close();
        }

        // temARRAY_TOO_LARGE: Batch: signers array exceeds 8 entries.
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const batchFee = batch::calcBatchFee(env, 0, 9);
            auto const aliceSeq = env.seq(alice);
            auto jt = env.jtnofill(
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::inner(pay(alice, bob, XRP(5)), aliceSeq + 2),
                batch::sig(bob, bob, bob, bob, bob, bob, bob, bob, bob, bob));

            env.app().openLedger().modify(
                [&](OpenView& view, beast::Journal j) {
                    auto const result =
                        ripple::apply(env.app(), view, *jt.stx, tapNONE, j);
                    BEAST_EXPECT(
                        !result.applied && result.ter == temARRAY_TOO_LARGE);
                    return result.applied;
                });
        }
    }

    void
    testAllOrNothing(FeatureBitset features)
    {
        testcase("all or nothing");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(1000), alice, bob);
        env.close();

        // all
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 3);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
        }

        // tec failure
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(999)), seq + 2),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {};
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequence
            BEAST_EXPECT(env.seq(alice) == seq + 1);

            // Alice pays Fee; Bob should not be affected
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob);
        }

        // tem failure
        {
            XRPAmount const feeDrops = env.current()->fees().base;

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const seq = env.seq(alice);
            auto tx1 = pay(alice, bob, XRP(1));
            tx1[jss::Fee] = to_string(feeDrops);
            tx1[jss::Sequence] = seq + 2;
            tx1[jss::SigningPubKey] = "";
            tx1[jss::Flags] = tx1[jss::Flags].asUInt() | tfInnerBatchTxn;
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner_nofill(tx1),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {};
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequence
            BEAST_EXPECT(env.seq(alice) == seq + 1);

            // Alice pays Fee; Bob should not be affected
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob);
        }
    }

    void
    testOnlyOne(FeatureBitset features)
    {
        testcase("only one");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(1000), alice, bob);
        env.close();

        // tec failure
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 3);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfOnlyOne),
                batch::inner(pay(alice, bob, XRP(999)), seq + 1),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                batch::inner(pay(alice, bob, XRP(2)), seq + 3),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tecUNFUNDED_PAYMENT", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 3);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(1));
        }

        // tem failure
        {
            XRPAmount const feeDrops = env.current()->fees().base;

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 3);
            auto const seq = env.seq(alice);
            auto tx1 = pay(alice, bob, XRP(1));
            tx1[jss::Fee] = to_string(feeDrops);
            tx1[jss::Sequence] = seq + 1;
            tx1[jss::SigningPubKey] = "";
            tx1[jss::Flags] = tx1[jss::Flags].asUInt() | tfInnerBatchTxn;
            env(batch::outer(alice, seq, batchFee, tfOnlyOne),
                batch::inner_nofill(tx1),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                batch::inner(pay(alice, bob, XRP(2)), seq + 3),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                // tx #1 fails with temINVALID_INNER_BATCH
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 1);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob);
        }
    }

    void
    testUntilFailure(FeatureBitset features)
    {
        testcase("until failure");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(1000), alice, bob);
        env.close();

        // tec error
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 4);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfUntilFailure),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                batch::inner(pay(alice, bob, XRP(999)), seq + 3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 4),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
                {"tecUNFUNDED_PAYMENT", to_string(txIDs[2])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 4);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
        }

        // tem error
        {
            XRPAmount const feeDrops = env.current()->fees().base;

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 4);
            auto const seq = env.seq(alice);
            auto tx3 = pay(alice, bob, XRP(1));
            tx3[jss::Fee] = to_string(feeDrops);
            tx3[jss::Sequence] = seq + 3;
            tx3[jss::SigningPubKey] = "";
            tx3[jss::Flags] = tx3[jss::Flags].asUInt() | tfInnerBatchTxn;
            env(batch::outer(alice, seq, batchFee, tfUntilFailure),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                batch::inner_nofill(tx3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 4),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
                // tx #3 fails with temINVALID_INNER_BATCH
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 3);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
        }
    }

    void
    testIndependent(FeatureBitset features)
    {
        testcase("independent");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(1000), alice, bob);
        env.close();

        // tec error
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 4);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfIndependent),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                batch::inner(pay(alice, bob, XRP(999)), seq + 3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 4),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
                {"tecUNFUNDED_PAYMENT", to_string(txIDs[2])},
                {"tesSUCCESS", to_string(txIDs[3])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 5);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(6) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(6));
        }

        // tem error
        {
            XRPAmount const feeDrops = env.current()->fees().base;

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 4);
            auto const seq = env.seq(alice);
            auto tx3 = pay(alice, bob, XRP(1));
            tx3[jss::Fee] = to_string(feeDrops);
            tx3[jss::Sequence] = seq + 3;
            tx3[jss::SigningPubKey] = "";
            tx3[jss::Flags] = tx3[jss::Flags].asUInt() | tfInnerBatchTxn;
            env(batch::outer(alice, seq, batchFee, tfIndependent),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                batch::inner_nofill(tx3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 4),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
                // tx #3 fails with temINVALID_INNER_BATCH
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 3);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
        }
    }

    void
    testBatchType(FeatureBitset features)
    {
        testcase("batch type");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const eve = Account("eve");
        env.fund(XRP(100000), alice, bob, carol, eve);
        env.close();

        {  // All or Nothing: all succeed
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const preCarol = env.balance(carol);
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(100)), seq + 1),
                batch::inner(pay(alice, carol, XRP(100)), seq + 2));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(200) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(100));
            BEAST_EXPECT(env.balance(carol) == preCarol + XRP(100));
        }

        {  // All or Nothing: one fails
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const preCarol = env.balance(carol);
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(100)), seq + 1),
                batch::inner(pay(alice, carol, XRP(747681)), seq + 2));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {};
            env.close();
            validateBatch(env, parentBatchId, testCases);

            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob);
            BEAST_EXPECT(env.balance(carol) == preCarol);
        }

        {  // Independent (one fails)
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const preCarol = env.balance(carol);
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 3);
            env(batch::outer(alice, seq, batchFee, tfIndependent),
                batch::inner(pay(alice, bob, XRP(100)), seq + 1),
                batch::inner(pay(alice, carol, XRP(100)), seq + 2),
                batch::inner(
                    offer(
                        alice,
                        alice["USD"](100),
                        XRP(100),
                        tfImmediateOrCancel),
                    seq + 3));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
                {"tecKILLED", to_string(txIDs[2])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(200) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(100));
            BEAST_EXPECT(env.balance(carol) == preCarol + XRP(100));
        }

        {  // Until Failure: one fails, one is not executed
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const preCarol = env.balance(carol);
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 4);
            env(batch::outer(alice, seq, batchFee, tfUntilFailure),
                batch::inner(pay(alice, bob, XRP(100)), seq + 1),
                batch::inner(pay(alice, carol, XRP(100)), seq + 2),
                batch::inner(
                    offer(
                        alice,
                        alice["USD"](100),
                        XRP(100),
                        tfImmediateOrCancel),
                    seq + 3),
                batch::inner(pay(alice, eve, XRP(100)), seq + 4));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
                {"tecKILLED", to_string(txIDs[2])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(200) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(100));
            BEAST_EXPECT(env.balance(carol) == preCarol + XRP(100));
        }

        {  // Only one: the fourth succeeds
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const preCarol = env.balance(carol);
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 6);
            env(batch::outer(alice, seq, batchFee, tfOnlyOne),
                batch::inner(
                    offer(
                        alice,
                        alice["USD"](100),
                        XRP(100),
                        tfImmediateOrCancel),
                    seq + 1),
                batch::inner(
                    offer(
                        alice,
                        alice["USD"](100),
                        XRP(100),
                        tfImmediateOrCancel),
                    seq + 2),
                batch::inner(
                    offer(
                        alice,
                        alice["USD"](100),
                        XRP(100),
                        tfImmediateOrCancel),
                    seq + 3),
                batch::inner(pay(alice, bob, XRP(100)), seq + 4),
                batch::inner(pay(alice, carol, XRP(100)), seq + 5),
                batch::inner(pay(alice, eve, XRP(100)), seq + 6));

            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tecKILLED", to_string(txIDs[0])},
                {"tecKILLED", to_string(txIDs[1])},
                {"tecKILLED", to_string(txIDs[2])},
                {"tesSUCCESS", to_string(txIDs[3])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(100) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(100));
            BEAST_EXPECT(env.balance(carol) == preCarol);
        }
    }

    void
    testInnerSubmitRPC(FeatureBitset features)
    {
        testcase("inner submit rpc");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);
        env.close();
        // Invalid RPC Submission: TxnSignature
        // - has `TxnSignature` field
        // - has no `SigningPubKey` field
        // - has no `Signers` field
        // - has `tfInnerBatchTxn` flag
        {
            auto jv = pay(alice, bob, XRP(1));
            jv[sfFlags.fieldName] = tfInnerBatchTxn;
            Serializer s;
            auto jt = env.jt(jv);
            jt.stx->add(s);
            auto const jrr = env.rpc("submit", strHex(s.slice()))[jss::result];
            BEAST_EXPECT(
                jrr[jss::status] == "error" &&
                jrr[jss::error] == "invalidTransaction" &&
                jrr[jss::error_exception] ==
                    "fails local checks: Malformed: Invalid inner batch "
                    "transaction.");

            env.close();
        }

        // Invalid RPC Submission: SigningPubKey
        // - has no `TxnSignature` field
        // - has `SigningPubKey` field
        // - has no `Signers` field
        // - has `tfInnerBatchTxn` flag
        {
            std::string txBlob =
                "1200002240000000240000000561D4838D7EA4C68000000000000000000000"
                "0000005553440000000000A407AF5856CCF3C42619DAA925813FC955C72983"
                "68400000000000000A73210388935426E0D08083314842EDFBB2D517BD4769"
                "9F9A4527318A8E10468C97C0528114AE123A8556F3CF91154711376AFB0F89"
                "4F832B3D8314F51DFC2A09D62CBBA1DFBDD4691DAC96AD98B90F";
            auto const jrr = env.rpc("submit", txBlob)[jss::result];
            BEAST_EXPECT(
                jrr[jss::status] == "error" &&
                jrr[jss::error] == "invalidTransaction" &&
                jrr[jss::error_exception] ==
                    "fails local checks: Malformed: Invalid inner batch "
                    "transaction.");

            env.close();
        }

        // Invalid RPC Submission: Signers
        // - has no `TxnSignature` field
        // - has empty `SigningPubKey` field
        // - has `Signers` field
        // - has `tfInnerBatchTxn` flag
        {
            std::string txBlob =
                "1200002240000000240000000561D4838D7EA4C68000000000000000000000"
                "0000005553440000000000A407AF5856CCF3C42619DAA925813FC955C72983"
                "68400000000000000A73008114AE123A8556F3CF91154711376AFB0F894F83"
                "2B3D8314F51DFC2A09D62CBBA1DFBDD4691DAC96AD98B90FF3E01073210289"
                "49021029D5CC87E78BCF053AFEC0CAFD15108EC119EAAFEC466F5C095407BF"
                "74473045022100EC791DC3306E1784B813CBE275C9A0E2F467EF795E3571AA"
                "DB295862F2F316350220668716954E02AF714F119F34D869891C8704A7989B"
                "DB0DBA029A7580430BB7138114B389FBCED0AF9DCDFF62900BFAEFA3EB872D"
                "8A96E1E010732102691AC5AE1C4C333AE5DF8A93BDC495F0EEBFC6DB0DA7EB"
                "6EF808F3AFC006E3FE74473045022100B93117804900BE1E83E5E2B5846642"
                "7BBFE2138CDEF5F31F566B4AC49A947C300220463AFD847028A76F3FEC997B"
                "56FA4C4E6514A57E77D38AC854A6A2A54DD4DB478114F51DFC2A09D62CBBA1"
                "DFBDD4691DAC96AD98B90FE1F1";
            auto const jrr = env.rpc("submit", txBlob)[jss::result];
            BEAST_EXPECT(
                jrr[jss::status] == "error" &&
                jrr[jss::error] == "invalidTransaction" &&
                jrr[jss::error_exception] ==
                    "fails local checks: Malformed: Invalid inner batch "
                    "transaction.");

            env.close();
        }

        // Invalid RPC Submission: tfInnerBatchTxn
        // - has no `TxnSignature` field
        // - has empty `SigningPubKey` field
        // - has no `Signers` field
        // - has `tfInnerBatchTxn` flag
        {
            std::string txBlob =
                "1200002240000000240000000561D4838D7EA4C68000000000000000000000"
                "0000005553440000000000A407AF5856CCF3C42619DAA925813FC955C72983"
                "68400000000000000A73008114AE123A8556F3CF91154711376AFB0F894F83"
                "2B3D8314F51DFC2A09D62CBBA1DFBDD4691DAC96AD98B90F";
            auto const jrr = env.rpc("submit", txBlob)[jss::result];
            BEAST_EXPECT(
                jrr[jss::status] == "success" &&
                jrr[jss::engine_result] == "temINVALID_FLAG");

            env.close();
        }
    }

    void
    testAccountActivation(FeatureBitset features)
    {
        testcase("account activation");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice);
        env.close();
        env.memoize(bob);

        auto const preAlice = env.balance(alice);

        // Tx 1
        Json::Value tx1 = noop(bob);
        tx1[sfSetFlag.fieldName] = asfAllowTrustLineClawback;

        auto const ledSeq = env.current()->seq();
        auto const seq = env.seq(alice);
        auto const batchFee = batch::calcBatchFee(env, 1, 2);
        env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
            batch::inner(pay(alice, bob, XRP(1000)), seq + 1),
            batch::inner(tx1, ledSeq),
            batch::sig(bob));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();
        validateBatch(env, parentBatchId, testCases);

        // Alice consumes sequences (# of txns)
        BEAST_EXPECT(env.seq(alice) == seq + 2);

        // Bob consumes sequences (# of txns)
        BEAST_EXPECT(env.seq(bob) == ledSeq + 1);

        // Alice pays XRP & Fee; Bob receives XRP
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1000) - batchFee);
        BEAST_EXPECT(env.balance(bob) == XRP(1000));
    }

    void
    testAccountSet(FeatureBitset features)
    {
        testcase("account set");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        // Tx 1
        Json::Value tx1 = noop(alice);
        std::string const domain = "example.com";
        tx1[sfDomain.fieldName] = strHex(domain);

        auto const seq = env.seq(alice);
        auto const batchFee = batch::calcBatchFee(env, 0, 2);
        env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
            batch::inner(tx1, seq + 1),
            batch::inner(pay(alice, bob, XRP(1)), seq + 2));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();
        validateBatch(env, parentBatchId, testCases);

        auto const sle = env.le(keylet::account(alice));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(
            sle->getFieldVL(sfDomain) == Blob(domain.begin(), domain.end()));

        // Alice consumes sequences (# of txns)
        BEAST_EXPECT(env.seq(alice) == seq + 3);

        // Alice pays XRP & Fee; Bob receives XRP
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1) - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(1));
    }

    void
    testAccountDelete(FeatureBitset features)
    {
        testcase("account delete");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(1000), alice, bob);
        env.close();

        // Close enough ledgers to delete account
        int const delta = [&]() -> int {
            if (env.seq(alice) + 300 > env.current()->seq())
                return env.seq(alice) - env.current()->seq() + 300;
            return 0;
        }();
        for (int i = 0; i < delta; ++i)
            env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const seq = env.seq(alice);
        auto const batchFee =
            batch::calcBatchFee(env, 0, 2) + env.current()->fees().increment;
        env(batch::outer(alice, seq, batchFee, tfIndependent),
            batch::inner(pay(alice, bob, XRP(1)), seq + 1),
            batch::inner(acctdelete(alice, bob), seq + 2),
            batch::inner(pay(alice, bob, XRP(2)), seq + 3));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();
        validateBatch(env, parentBatchId, testCases);

        // Alice does not exist; Bob receives Alice's XRP
        BEAST_EXPECT(!env.le(keylet::account(alice)));
        BEAST_EXPECT(env.balance(bob) == preBob + (preAlice - batchFee));
    }

    static uint256
    getCheckIndex(AccountID const& account, std::uint32_t uSequence)
    {
        return keylet::check(account, uSequence).key;
    }

    void
    testObjectCreateSequence(FeatureBitset features)
    {
        testcase("object create w/ sequence");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        env.fund(XRP(1000), alice, bob, gw);
        env.close();

        env.trust(USD(1000), alice, bob);
        env(pay(gw, alice, USD(100)));
        env(pay(gw, bob, USD(100)));
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const bobSeq = env.seq(bob);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        auto const preAliceUSD = env.balance(alice, USD.issue());
        auto const preBobUSD = env.balance(bob, USD.issue());

        auto const batchFee = batch::calcBatchFee(env, 1, 2);
        uint256 const chkId{getCheckIndex(bob, env.seq(bob))};
        env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
            batch::inner(check::create(bob, alice, USD(10)), bobSeq),
            batch::inner(check::cash(alice, chkId, USD(10)), aliceSeq + 1),
            batch::sig(bob));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();
        validateBatch(env, parentBatchId, testCases);

        // Alice consumes sequences (# of txns)
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 2);

        // Alice consumes sequences (# of txns)
        BEAST_EXPECT(env.seq(bob) == bobSeq + 1);

        // Alice pays Fee; Bob XRP Unchanged
        BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob);

        // Alice pays USD & Bob receives USD
        BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD + USD(10));
        BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD - USD(10));
    }

    void
    testObjectCreateTicket(FeatureBitset features)
    {
        testcase("object create w/ ticket");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        env.fund(XRP(1000), alice, bob, gw);
        env.close();

        env.trust(USD(1000), alice, bob);
        env(pay(gw, alice, USD(100)));
        env(pay(gw, bob, USD(100)));
        env.close();

        std::uint32_t bobTicketSeq{env.seq(bob) + 1};
        env(ticket::create(bob, 10));
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const bobSeq = env.seq(bob);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        auto const preAliceUSD = env.balance(alice, USD.issue());
        auto const preBobUSD = env.balance(bob, USD.issue());

        auto const batchFee = batch::calcBatchFee(env, 1, 2);
        uint256 const chkId{getCheckIndex(bob, bobTicketSeq)};
        env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
            batch::inner(check::create(bob, alice, USD(10)), 0, bobTicketSeq),
            batch::inner(check::cash(alice, chkId, USD(10)), aliceSeq + 1),
            batch::sig(bob));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();
        validateBatch(env, parentBatchId, testCases);

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 2);
        BEAST_EXPECT(env.seq(bob) == bobSeq);
        BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob);
        BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD + USD(10));
        BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD - USD(10));
    }

    void
    testObjectCreate3rdParty(FeatureBitset features)
    {
        testcase("object create w/ 3rd party");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        env.fund(XRP(1000), alice, bob, carol, gw);
        env.close();

        env.trust(USD(1000), alice, bob);
        env(pay(gw, alice, USD(100)));
        env(pay(gw, bob, USD(100)));
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const bobSeq = env.seq(bob);
        auto const carolSeq = env.seq(carol);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        auto const preCarol = env.balance(carol);
        auto const preAliceUSD = env.balance(alice, USD.issue());
        auto const preBobUSD = env.balance(bob, USD.issue());

        auto const batchFee = batch::calcBatchFee(env, 2, 2);
        uint256 const chkId{getCheckIndex(bob, env.seq(bob))};
        env(batch::outer(carol, carolSeq, batchFee, tfAllOrNothing),
            batch::inner(check::create(bob, alice, USD(10)), bobSeq),
            batch::inner(check::cash(alice, chkId, USD(10)), aliceSeq),
            batch::sig(alice, bob));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();
        validateBatch(env, parentBatchId, testCases);

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        BEAST_EXPECT(env.seq(bob) == bobSeq + 1);
        BEAST_EXPECT(env.seq(carol) == carolSeq + 1);
        BEAST_EXPECT(env.balance(alice) == preAlice);
        BEAST_EXPECT(env.balance(bob) == preBob);
        BEAST_EXPECT(env.balance(carol) == preCarol - batchFee);
        BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD + USD(10));
        BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD - USD(10));
    }

    void
    testTicketsOuter(FeatureBitset features)
    {
        testcase("tickets outer");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);
        env.close();

        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 10));
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const batchFee = batch::calcBatchFee(env, 0, 2);
        env(batch::outer(alice, 0, batchFee, tfAllOrNothing),
            batch::inner(pay(alice, bob, XRP(1)), aliceSeq + 0),
            batch::inner(pay(alice, bob, XRP(2)), aliceSeq + 1),
            ticket::use(aliceTicketSeq++));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();
        validateBatch(env, parentBatchId, testCases);

        auto const sle = env.le(keylet::account(alice));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getFieldU32(sfOwnerCount) == 9);
        BEAST_EXPECT(sle->getFieldU32(sfTicketCount) == 9);

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 2);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
    }

    void
    testTicketsInner(FeatureBitset features)
    {
        testcase("tickets inner");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);
        env.close();

        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 10));
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const batchFee = batch::calcBatchFee(env, 0, 2);
        env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
            batch::inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq),
            batch::inner(pay(alice, bob, XRP(2)), 0, aliceTicketSeq + 1));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();
        validateBatch(env, parentBatchId, testCases);

        auto const sle = env.le(keylet::account(alice));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getFieldU32(sfOwnerCount) == 8);
        BEAST_EXPECT(sle->getFieldU32(sfTicketCount) == 8);

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
    }

    void
    testTicketsOuterInner(FeatureBitset features)
    {
        testcase("tickets outer inner");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);
        env.close();

        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 10));
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const batchFee = batch::calcBatchFee(env, 0, 2);
        env(batch::outer(alice, 0, batchFee, tfAllOrNothing),
            batch::inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
            batch::inner(pay(alice, bob, XRP(2)), aliceSeq),
            ticket::use(aliceTicketSeq));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();
        validateBatch(env, parentBatchId, testCases);

        auto const sle = env.le(keylet::account(alice));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getFieldU32(sfOwnerCount) == 8);
        BEAST_EXPECT(sle->getFieldU32(sfTicketCount) == 8);

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
    }

    void
    testPseudoTxn(FeatureBitset features)
    {
        testcase("pseudo txn");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(1000), alice, bob);
        env.close();

        STTx const stx = STTx(ttAMENDMENT, [&](auto& obj) {
            obj.setAccountID(sfAccount, AccountID());
            obj.setFieldH256(sfAmendment, uint256(2));
            obj.setFieldU32(sfLedgerSequence, env.seq(alice));
            obj.setFieldU32(sfFlags, tfInnerBatchTxn);
        });

        std::string reason;
        BEAST_EXPECT(isPseudoTx(stx));
        BEAST_EXPECT(!passesLocalChecks(stx, reason));
        BEAST_EXPECT(reason == "Cannot submit pseudo transactions.");
        env.app().openLedger().modify([&](OpenView& view, beast::Journal j) {
            auto const result = ripple::apply(env.app(), view, stx, tapNONE, j);
            BEAST_EXPECT(!result.applied && result.ter == temINVALID_FLAG);
            return result.applied;
        });
    }

    void
    testBatchWithSelfSubmit(FeatureBitset features)
    {
        testcase("batch with self submit");
        // IMPORTANT: When a transaction is self submitted and another
        // transaction is part of the batch, the batch might fail because the
        // sequence is out of order. This is because the canonical order of
        // transactions is determined by the account first. So in this case,
        // alice's batch comes after bobs self submitted transaction even though
        // the self submitted was after the batch.

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};
        XRPAmount const baseFee = env.current()->fees().base;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);
        env.close();

        env(noop(bob), ter(tesSUCCESS));
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        auto const bobSeq = env.seq(bob);

        // Alice Pays Bob (Self Submit)
        env(pay(alice, bob, XRP(10)), ter(tesSUCCESS));

        // Alice & Bob Atomic Batch
        auto const batchFee = batch::calcBatchFee(env, 1, 2);
        env(batch::outer(alice, aliceSeq + 1, batchFee, tfAllOrNothing),
            batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 2),
            batch::inner(pay(bob, alice, XRP(5)), bobSeq + 1),
            batch::sig(bob));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };

        // Bob pays Alice (Self Submit)
        env(pay(bob, alice, XRP(5)), ter(tesSUCCESS));
        env.close();

        validateBatch(env, parentBatchId, testCases, 2);

        // Alice consumes sequences (# of txns)
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 3);

        // Alice consumes sequences (# of txns)
        BEAST_EXPECT(env.seq(bob) == bobSeq + 2);

        // Alice pays XRP & Fee; Bob receives XRP & pays Fee
        BEAST_EXPECT(
            env.balance(alice) == preAlice - XRP(10) - batchFee - baseFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(10) - baseFee);
    }

    static std::unique_ptr<Config>
    makeConfig(
        std::map<std::string, std::string> extraTxQ = {},
        std::map<std::string, std::string> extraVoting = {})
    {
        auto p = test::jtx::envconfig();
        auto& section = p->section("transaction_queue");
        section.set("ledgers_in_queue", "2");
        section.set("minimum_queue_size", "2");
        section.set("min_ledgers_to_compute_size_limit", "3");
        section.set("max_ledger_counts_to_store", "100");
        section.set("retry_sequence_percent", "25");
        section.set("normal_consensus_increase_percent", "0");

        for (auto const& [k, v] : extraTxQ)
            section.set(k, v);

        return p;
    }

    void
    checkMetrics(
        int line,
        jtx::Env& env,
        std::size_t expectedCount,
        std::optional<std::size_t> expectedMaxCount,
        std::size_t expectedInLedger)
    {
        auto const metrics = env.app().getTxQ().getMetrics(*env.current());
        using namespace std::string_literals;
        metrics.txCount == expectedCount
            ? pass()
            : fail(
                  "txCount: "s + std::to_string(metrics.txCount) + "/" +
                      std::to_string(expectedCount),
                  __FILE__,
                  line);
        metrics.txQMaxSize == expectedMaxCount
            ? pass()
            : fail(
                  "txQMaxSize: "s +
                      std::to_string(metrics.txQMaxSize.value_or(0)) + "/" +
                      std::to_string(expectedMaxCount.value_or(0)),
                  __FILE__,
                  line);
        metrics.txInLedger == expectedInLedger
            ? pass()
            : fail(
                  "txInLedger: "s + std::to_string(metrics.txInLedger) + "/" +
                      std::to_string(expectedInLedger),
                  __FILE__,
                  line);
    }

    auto
    openLedgerFee(jtx::Env& env, XRPAmount const& batchFee)
    {
        using namespace jtx;

        auto const& view = *env.current();
        auto metrics = env.app().getTxQ().getMetrics(view);
        return toDrops(metrics.openLedgerFeeLevel, batchFee) + 1;
    }

    void
    testBatchTxQueue(FeatureBitset features)
    {
        testcase("batch tx queue");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{
            *this,
            makeConfig({{"minimum_txn_in_ledger_standalone", "2"}}),
            nullptr,
            beast::severities::kError};

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto carol = Account("carol");

        auto queued = ter(terQUEUED);

        // Fund across several ledgers so the TxQ metrics stay restricted.
        env.fund(XRP(1000), noripple(alice, bob));
        env.close(env.now() + 5s, 10000ms);
        env.fund(XRP(1000), noripple(carol));
        env.close(env.now() + 5s, 10000ms);

        // Fill the ledger
        env(noop(alice));
        env(noop(alice));
        env(noop(alice));
        checkMetrics(__LINE__, env, 0, std::nullopt, 3);

        env(noop(carol), queued);
        checkMetrics(__LINE__, env, 1, std::nullopt, 3);

        auto const aliceSeq = env.seq(alice);
        auto const bobSeq = env.seq(bob);
        auto const batchFee = batch::calcBatchFee(env, 1, 2);

        // Queue Batch
        {
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::inner(pay(bob, alice, XRP(5)), bobSeq),
                batch::sig(bob),
                queued);
        }

        checkMetrics(__LINE__, env, 2, std::nullopt, 3);

        // Replace Queued Batch
        {
            env(batch::outer(
                    alice,
                    aliceSeq,
                    openLedgerFee(env, batchFee),
                    tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::inner(pay(bob, alice, XRP(5)), bobSeq),
                batch::sig(bob),
                ter(tesSUCCESS));
            env.close();
        }

        checkMetrics(__LINE__, env, 0, 12, 1);
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testEnable(features);
        testPreflight(features);
        testPreclaim(features);
        testBadRawTxn(features);
        testBadSequence(features);
        testBadOuterFee(features);
        testBadInnerFee(features);
        testCalculateBaseFee(features);
        testAllOrNothing(features);
        testOnlyOne(features);
        testUntilFailure(features);
        testIndependent(features);
        testBatchType(features);
        testInnerSubmitRPC(features);
        testAccountActivation(features);
        testAccountSet(features);
        testAccountDelete(features);
        testObjectCreateSequence(features);
        testObjectCreateTicket(features);
        testObjectCreate3rdParty(features);
        testTicketsOuter(features);
        testTicketsInner(features);
        testTicketsOuterInner(features);
        testPseudoTxn(features);
        testBatchWithSelfSubmit(features);
        testBatchTxQueue(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = supported_amendments();
        testWithFeats(sa);
    }
};

BEAST_DEFINE_TESTSUITE(Batch, app, ripple);

}  // namespace test
}  // namespace ripple