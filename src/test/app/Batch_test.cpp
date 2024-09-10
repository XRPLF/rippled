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
#include <xrpl/protocol/Feature.h>
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
        std::string txType;
        std::string hash;
    };

    struct TestSignData
    {
        int index;
        jtx::Account account;
    };

    Json::Value
    getTxByIndex(Json::Value jrr, std::uint8_t index)
    {
        for (auto const& txn : jrr[jss::result][jss::ledger][jss::transactions])
        {
            if (txn[jss::metaData][sfTransactionIndex.jsonName] == index)
                return txn;
        }
        return {};
    }

    void
    validateBatchTxns(
        Json::Value meta,
        std::uint32_t const& txns,
        std::vector<TestBatchData> const& batchResults)
    {
        BEAST_EXPECT(meta[sfBatchExecutions.jsonName].size() != txns);
        size_t index = 0;
        for (auto const& _batchTxn : meta[sfBatchExecutions.jsonName])
        {
            auto const b = _batchTxn[sfBatchExecution.jsonName];
            BEAST_EXPECT(
                b[sfInnerResult.jsonName] ==
                strHex(batchResults[index].result));
            BEAST_EXPECT(
                b[sfTransactionType.jsonName] == batchResults[index].txType);
            if (batchResults[index].hash != "")
                BEAST_EXPECT(
                    b[sfTransactionHash.jsonName] == batchResults[index].hash);
            ++index;
        }
    }

    void
    validateBatchMeta(
        Json::Value meta,
        STAmount const& balance,
        std::uint32_t const& sequence,
        std::optional<std::uint32_t> ownerCount = std::nullopt,
        std::optional<std::uint32_t> ticketCount = std::nullopt)
    {
        for (Json::Value const& node : meta[sfAffectedNodes.jsonName])
        {
            if (node.isMember(sfModifiedNode.jsonName))
            {
                Json::Value const& modified = node[sfModifiedNode.jsonName];
                std::string const entryType =
                    modified[sfLedgerEntryType.jsonName].asString();
                if (entryType == jss::AccountRoot)
                {
                    auto const& previousFields =
                        modified[sfPreviousFields.jsonName];
                    std::uint32_t const prevSeq =
                        previousFields[sfSequence.jsonName].asUInt();
                    BEAST_EXPECT(prevSeq == sequence);
                    if (ownerCount.has_value())
                        BEAST_EXPECT(
                            previousFields[sfOwnerCount.jsonName].asUInt() ==
                            *ownerCount);
                    if (ticketCount.has_value())
                        BEAST_EXPECT(
                            previousFields[sfTicketCount.jsonName].asUInt() ==
                            *ticketCount);
                }
            }
        }
    }

    Json::Value
    addBatchTx(
        Json::Value jv,
        Json::Value const& tx,
        jtx::Account const& outerAccount,
        std::uint8_t batchIndex,
        std::uint32_t sequence,
        std::optional<std::uint32_t> ticket = std::nullopt)
    {
        std::uint32_t const index = jv[jss::RawTransactions].size();
        Json::Value& batchTransaction = jv[jss::RawTransactions][index];

        // Initialize the batch transaction
        batchTransaction = Json::Value{};
        batchTransaction[jss::RawTransaction] = tx;
        batchTransaction[jss::RawTransaction][jss::SigningPubKey] = "";
        batchTransaction[jss::RawTransaction][sfFee.jsonName] = 0;
        batchTransaction[jss::RawTransaction][jss::Sequence] = 0;

        // Set batch transaction details
        auto& batchTxn =
            batchTransaction[jss::RawTransaction][sfBatchTxn.jsonName];
        batchTxn = Json::Value{};
        batchTxn[sfOuterAccount.jsonName] = outerAccount.human();
        batchTxn[sfBatchIndex.jsonName] = batchIndex;
        batchTxn[sfSequence.jsonName] = sequence;

        // Optionally set ticket sequence
        if (ticket.has_value())
        {
            batchTxn[sfTicketSequence.jsonName] = *ticket;
        }

        return jv;
    }

    Json::Value
    addBatchSignatures(Json::Value jv, std::vector<TestSignData> const& signers)
    {
        auto const ojv = jv;
        for (auto const& signer : signers)
        {
            Serializer ss{
                buildMultiSigningData(jtx::parse(ojv), signer.account.id())};
            auto const sig = ripple::sign(
                signer.account.pk(), signer.account.sk(), ss.slice());
            jv[sfBatchSigners.jsonName][signer.index][sfBatchSigner.jsonName]
              [sfAccount.jsonName] = signer.account.human();
            jv[sfBatchSigners.jsonName][signer.index][sfBatchSigner.jsonName]
              [sfSigningPubKey.jsonName] = strHex(signer.account.pk());
            jv[sfBatchSigners.jsonName][signer.index][sfBatchSigner.jsonName]
              [sfTxnSignature.jsonName] = strHex(Slice{sig.data(), sig.size()});
        }
        return jv;
    }

    Json::Value
    addBatchMultiSignatures(
        Json::Value jv,
        int index,
        jtx::Account account,
        std::vector<TestSignData> const& signers)
    {
        auto const ojv = jv;
        Json::Value jvSigners = Json::arrayValue;
        for (std::size_t i = 0; i < signers.size(); ++i)
        {
            Serializer ss{buildMultiSigningData(
                jtx::parse(ojv), signers[i].account.id())};
            auto const sig = ripple::sign(
                signers[i].account.pk(), signers[i].account.sk(), ss.slice());

            jvSigners[i][sfSigner.jsonName][sfAccount.jsonName] =
                signers[i].account.human();
            jvSigners[i][sfSigner.jsonName][sfSigningPubKey.jsonName] =
                strHex(signers[i].account.pk());
            jvSigners[i][sfSigner.jsonName][sfTxnSignature.jsonName] =
                strHex(Slice{sig.data(), sig.size()});
        }
        jv[sfBatchSigners.jsonName][index][sfBatchSigner.jsonName]
          [sfAccount.jsonName] = account.human();
        jv[sfBatchSigners.jsonName][index][sfBatchSigner.jsonName]
          [sfSigners.jsonName] = jvSigners;
        return jv;
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

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const seq = env.seq(alice);
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            // Batch Transactions
            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            // Tx 1
            Json::Value const tx1 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx1, alice, 1, seq);

            auto const txResult =
                withBatch ? ter(tesSUCCESS) : ter(temDISABLED);
            env(jv, txResult);
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

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        // temINVALID_FLAG: Batch: invalid flags.
        {
            auto const seq = env.seq(alice);
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            env(jv, txflags(tfRequireAuth), ter(temINVALID_FLAG));
            env.close();
        }

        // temMALFORMED: Batch: txns array empty.
        {
            auto const seq = env.seq(alice);
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            env(jv, ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: Batch: txns array exceeds 12 entries.
        {
            auto const seq = env.seq(alice);
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};
            for (std::uint8_t i = 1; i < 13; ++i)
            {
                Json::Value const tx = pay(alice, bob, XRP(1));
                jv = addBatchTx(jv, tx, alice, i, seq);
            }

            env(jv, ter(temMALFORMED));
            env.close();
        }

        // temBAD_SIGNATURE: Batch: invalid batch txn signature.
        {
            std::vector<TestSignData> const signers = {{
                {0, carol},
            }};

            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = env.seq(alice);
            auto const batchFee =
                ((signers.size() + 2) * feeDrops) + feeDrops * 2;
            jv[jss::Fee] = to_string(batchFee);
            jv[jss::Flags] = tfAllOrNothing;
            jv[jss::SigningPubKey] = strHex(alice.pk());

            // Batch Transactions
            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            // Tx 1
            Json::Value tx1 = pay(alice, bob, XRP(10));
            jv = addBatchTx(jv, tx1, alice, 1, env.seq(alice));

            // Tx 2
            Json::Value const tx2 = pay(bob, alice, XRP(5));
            jv = addBatchTx(jv, tx2, alice, 0, env.seq(bob));

            for (auto const& signer : signers)
            {
                Serializer ss{
                    buildMultiSigningData(jtx::parse(jv), signer.account.id())};
                auto const sig = ripple::sign(
                    signer.account.pk(), signer.account.sk(), ss.slice());
                jv[sfBatchSigners.jsonName][signer.index]
                  [sfBatchSigner.jsonName][sfAccount.jsonName] =
                      signer.account.human();
                jv[sfBatchSigners.jsonName][signer.index]
                  [sfBatchSigner.jsonName][sfSigningPubKey.jsonName] =
                      strHex(alice.pk());
                jv[sfBatchSigners.jsonName][signer.index]
                  [sfBatchSigner.jsonName][sfTxnSignature.jsonName] =
                      strHex(Slice{sig.data(), sig.size()});
            }
            env(jv, ter(temBAD_SIGNATURE));
            env.close();
        }

        // temMALFORMED: Batch: TransactionType missing in array entry.
        // {
        //     auto const seq = env.seq(alice);
        //     Json::Value jv;
        //     jv[jss::TransactionType] = jss::Batch;
        //     jv[jss::Account] = alice.human();
        //     jv[jss::Sequence] = seq;

        //     jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        //     // Tx 1
        //     Json::Value tx1 = pay(alice, bob, XRP(10));
        //     // jv = addBatchTx(jv, tx1, alice, 0, seq, 0);
        //     jv[sfRawTransactions.jsonName][0u][jss::RawTransaction].removeMember(
        //         jss::TransactionType);

        //     env(jv, ter(temMALFORMED));
        //     env.close();
        // }

        // temMALFORMED: Batch: batch cannot have inner batch txn.
        {
            auto const seq = env.seq(alice);
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            // Batch Transactions
            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            // Tx 1
            Json::Value btx;
            {
                btx[jss::TransactionType] = jss::Batch;
                btx[jss::Account] = alice.human();
                btx[jss::Sequence] = seq;

                // Batch Transactions
                btx[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

                // bTx 1
                Json::Value const btx1 = pay(alice, bob, XRP(1));
                btx = addBatchTx(btx, btx1, alice, 0, seq, 0);
            }

            jv = addBatchTx(jv, btx, alice, 1, seq);

            // Tx 2
            Json::Value const tx2 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx2, alice, 2, seq);

            env(jv, ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: Batch: batch cannot have inner account delete txn.
        {
            auto const seq = env.seq(alice);
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            // Batch Transactions
            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            // Tx 1
            Json::Value tx1 = acctdelete(alice, bob);
            jv = addBatchTx(jv, tx1, alice, 1, seq);

            // Tx 2
            Json::Value const tx2 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx2, alice, 2, seq);

            env(jv, ter(temMALFORMED));
            env.close();
        }

        // temBAD_SIGNER: Batch: inner txn not signed by the right user.
        {
            std::vector<TestSignData> const signers = {{
                {0, carol},
            }};

            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = env.seq(alice);
            auto const batchFee =
                ((signers.size() + 2) * feeDrops) + feeDrops * 2;
            jv[jss::Fee] = to_string(batchFee);
            jv[jss::Flags] = tfAllOrNothing;
            jv[jss::SigningPubKey] = strHex(alice.pk());

            // Batch Transactions
            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            // Tx 1
            Json::Value tx1 = pay(alice, bob, XRP(10));
            jv = addBatchTx(jv, tx1, alice, 0, env.seq(alice), 0);

            // Tx 2
            Json::Value const tx2 = pay(bob, alice, XRP(5));
            jv = addBatchTx(jv, tx2, alice, 0, env.seq(bob), 1);

            jv = addBatchSignatures(jv, signers);
            env(jv, ter(temBAD_SIGNER));
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

        auto const feeDrops = env.current()->fees().base;
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

        auto const preAliceSeq = env.seq(alice);
        auto const preAlice = env.balance(alice);
        auto const preAliceUSD = env.balance(alice, USD.issue());
        auto const preBobSeq = env.seq(bob);
        auto const preBob = env.balance(bob);
        auto const preBobUSD = env.balance(bob, USD.issue());

        std::vector<TestSignData> const signers = {{
            {0, bob},
        }};

        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = preAliceSeq;
        auto const batchFee = ((signers.size() + 2) * feeDrops) + feeDrops * 2;
        jv[jss::Fee] = to_string(batchFee);
        jv[jss::Flags] = tfAllOrNothing;
        jv[jss::SigningPubKey] = strHex(alice.pk());

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value tx1 = pay(alice, bob, USD(10));
        jv = addBatchTx(jv, tx1, alice, 1, preAliceSeq);

        // Tx 2
        Json::Value const tx2 = pay(bob, alice, USD(5));
        jv = addBatchTx(jv, tx2, alice, 10, preBobSeq);

        jv = addBatchSignatures(jv, signers);

        // Internally telPRE_SEQ
        env(jv, ter(tecBATCH_FAILURE));
        env.close();

        // Alice pays fee & Bob should not be affected.
        BEAST_EXPECT(env.seq(alice) == preAliceSeq + 1);
        BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
        BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD);
        BEAST_EXPECT(env.seq(bob) == preBobSeq);
        BEAST_EXPECT(env.balance(bob) == preBob);
        BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD);
    }

    void
    testBadFee(FeatureBitset features)
    {
        testcase("bad fee");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
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

        auto const preAliceSeq = env.seq(alice);
        auto const preAlice = env.balance(alice);
        auto const preAliceUSD = env.balance(alice, USD.issue());
        auto const preBobSeq = env.seq(bob);
        auto const preBob = env.balance(bob);
        auto const preBobUSD = env.balance(bob, USD.issue());

        std::vector<TestSignData> const signers = {{
            {0, bob},
        }};

        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = env.seq(alice);
        jv[jss::Fee] = to_string(feeDrops * 2);
        jv[jss::Flags] = tfAllOrNothing;
        jv[jss::SigningPubKey] = strHex(alice.pk());

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value tx1 = pay(alice, bob, USD(10));
        jv = addBatchTx(jv, tx1, alice, 1, env.seq(alice));

        // Tx 2
        Json::Value const tx2 = pay(bob, alice, USD(5));
        jv = addBatchTx(jv, tx2, alice, 0, env.seq(bob));

        jv = addBatchSignatures(jv, signers);

        env(jv, ter(telINSUF_FEE_P));
        env.close();

        // Alice & Bob should not be affected.
        BEAST_EXPECT(env.seq(alice) == preAliceSeq);
        BEAST_EXPECT(env.balance(alice) == preAlice);
        BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD);
        BEAST_EXPECT(env.seq(bob) == preBobSeq);
        BEAST_EXPECT(env.balance(bob) == preBob);
        BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD);
    }

    void
    testOutOfSequence(FeatureBitset features)
    {
        testcase("out of sequence");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        // tfAllOrNothing
        {
            auto const seq = env.seq(alice);
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            // Batch Transactions
            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            // Tx 1
            Json::Value const tx1 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx1, alice, 1, seq);

            // Tx 2
            Json::Value const tx2 =
                jtx::trust(alice, alice["USD"](1000), tfSetfAuth);
            jv = addBatchTx(jv, tx2, alice, 2, seq);

            // Tx 3
            Json::Value const tx3 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx3, alice, 3, seq);

            // Internally tefNO_AUTH_REQUIRED
            env(jv,
                fee(feeDrops * 3),
                txflags(tfAllOrNothing),
                ter(tecBATCH_FAILURE));
            env.close();
        }

        // tfUntilFailure
        {
            auto const seq = env.seq(alice);
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            // Batch Transactions
            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            // Tx 1
            Json::Value const tx1 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx1, alice, 1, seq);

            // Tx 2
            Json::Value const tx2 =
                jtx::trust(alice, alice["USD"](1000), tfSetfAuth);
            jv = addBatchTx(jv, tx2, alice, 2, seq);

            // Tx 3
            Json::Value const tx3 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx3, alice, 3, seq);

            // Internally tefNO_AUTH_REQUIRED
            env(jv,
                fee(feeDrops * 3),
                txflags(tfUntilFailure),
                ter(tecBATCH_FAILURE));
            env.close();
        }

        // tfOnlyOne
        {
            auto const seq = env.seq(alice);
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            // Batch Transactions
            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            // Tx 1
            Json::Value const tx1 =
                jtx::trust(alice, alice["USD"](1000), tfSetfAuth);
            jv = addBatchTx(jv, tx1, alice, 1, seq);

            // Tx 2
            Json::Value const tx2 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx2, alice, 2, seq);

            // Internally tefNO_AUTH_REQUIRED
            env(jv,
                fee(feeDrops * 2),
                txflags(tfOnlyOne),
                ter(tecBATCH_FAILURE));
            env.close();
        }

        // tfIndependent
        {
            auto const seq = env.seq(alice);
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            // Batch Transactions
            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            // Tx 1
            Json::Value const tx1 =
                jtx::trust(alice, alice["USD"](1000), tfSetfAuth);
            jv = addBatchTx(jv, tx1, alice, 1, seq);

            // Tx 2
            Json::Value const tx2 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx2, alice, 2, seq);

            // Internally tefNO_AUTH_REQUIRED
            env(jv,
                fee(feeDrops * 3),
                txflags(tfIndependent),
                ter(tecBATCH_FAILURE));
            env.close();
        }
    }

    void
    testAllOrNothing(FeatureBitset features)
    {
        testcase("all or nothing");

        using namespace test::jtx;
        using namespace std::literals;

        // all
        {
            test::jtx::Env env{*this, envconfig()};

            auto const feeDrops = env.current()->fees().base;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const seq = env.seq(alice);
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            // Batch Transactions
            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            // Tx 1
            Json::Value const tx1 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx1, alice, 1, seq);

            // Tx 2
            Json::Value const tx2 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx2, alice, 2, seq);

            env(jv,
                fee(feeDrops * 2),
                txflags(tfAllOrNothing),
                ter(tesSUCCESS));
            env.close();

            std::vector<TestBatchData> testCases = {{
                {"tesSUCCESS",
                 "Payment",
                 "CF28B462454DC1651D1705E3C2BD49E0C4D91245C68D3A10D27CF56E5C9B5"
                 "BE5"},
                {"tesSUCCESS",
                 "Payment",
                 "002C79A3D4BB339E09C358450D96B885C21B7F5701B0E908DAC3DFE6C1360"
                 "7DA"},
            }};

            Json::Value params;
            params[jss::ledger_index] = env.current()->seq() - 1;
            params[jss::transactions] = true;
            params[jss::expand] = true;
            auto const jrr = env.rpc("json", "ledger", to_string(params));
            // std::cout << jrr << std::endl;
            auto const txn = getTxByIndex(jrr, 2);
            validateBatchTxns(txn[jss::metaData], 3, testCases);
            validateBatchMeta(txn[jss::metaData], preAlice, seq);

            BEAST_EXPECT(env.seq(alice) == 7);
            BEAST_EXPECT(
                env.balance(alice) == preAlice - XRP(2) - (feeDrops * 2));
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(2));
        }

        // nothing
        {
            test::jtx::Env env{*this, envconfig()};

            auto const feeDrops = env.current()->fees().base;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const seq = env.seq(alice);
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            // Batch Transactions
            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            // Tx 1
            Json::Value const tx1 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx1, alice, 1, seq);

            // Tx 2
            Json::Value const tx2 = pay(alice, bob, XRP(999));
            jv = addBatchTx(jv, tx2, alice, 2, seq);

            env(jv,
                fee(feeDrops * 2),
                txflags(tfAllOrNothing),
                ter(tecBATCH_FAILURE));
            env.close();

            std::vector<TestBatchData> testCases = {{
                {"tesSUCCESS",
                 "Payment",
                 "CF28B462454DC1651D1705E3C2BD49E0C4D91245C68D3A10D27CF56E5C9B5"
                 "BE5"},
                {"tecUNFUNDED_PAYMENT",
                 "Payment",
                 "68803BEF141614DBBB34FA34BE0E485D79A43328891A9A8BDC461B6F22836"
                 "A5C"},
            }};

            Json::Value params;
            params[jss::ledger_index] = env.current()->seq() - 1;
            params[jss::transactions] = true;
            params[jss::expand] = true;
            auto const jrr = env.rpc("json", "ledger", to_string(params));
            // std::cout << jrr << std::endl;
            auto const txn = getTxByIndex(jrr, 1);
            validateBatchTxns(txn[jss::metaData], 1, testCases);
            validateBatchMeta(txn[jss::metaData], preAlice, seq);

            BEAST_EXPECT(env.seq(alice) == 5);
            BEAST_EXPECT(env.balance(alice) == preAlice - (feeDrops * 2));
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

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const seq = env.seq(alice);
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = seq;

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 2
        Json::Value const tx1 = pay(alice, bob, XRP(999));
        jv = addBatchTx(jv, tx1, alice, 1, seq);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice, 2, seq);

        // Tx 3
        Json::Value const tx3 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx3, alice, 3, seq);

        env(jv, fee(feeDrops * 3), txflags(tfOnlyOne), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tecUNFUNDED_PAYMENT",
             "Payment",
             "093B51856BA4C111D626D933AC8D8EF8CCEB16B754EFE8A03819043E4927F50"
             "3"},
            {"tesSUCCESS",
             "Payment",
             "002C79A3D4BB339E09C358450D96B885C21B7F5701B0E908DAC3DFE6C13607D"
             "A"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 2);
        validateBatchTxns(txn[jss::metaData], 3, testCases);
        validateBatchMeta(txn[jss::metaData], preAlice, seq);

        BEAST_EXPECT(env.seq(alice) == 7);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1) - (feeDrops * 3));
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(1));
    }

    void
    testUntilFailure(FeatureBitset features)
    {
        testcase("until failure");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const seq = env.seq(alice);
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = seq;

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value const tx1 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx1, alice, 1, seq);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice, 2, seq);

        // Tx 3
        Json::Value const tx3 = pay(alice, bob, XRP(999));
        jv = addBatchTx(jv, tx3, alice, 3, seq);

        // Tx 4
        Json::Value const tx4 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx4, alice, 4, seq);

        env(jv, fee(feeDrops * 4), txflags(tfUntilFailure), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "Payment",
             "CF28B462454DC1651D1705E3C2BD49E0C4D91245C68D3A10D27CF56E5C9B5BE"
             "5"},
            {"tesSUCCESS",
             "Payment",
             "002C79A3D4BB339E09C358450D96B885C21B7F5701B0E908DAC3DFE6C13607D"
             "A"},
            {"tecUNFUNDED_PAYMENT",
             "Payment",
             "E6FC37AF2B22F398E7D32B89C73D2443DF8BE7A2F35CA8B0B6AF6E9A504A67F"
             "4"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 3);
        validateBatchTxns(txn[jss::metaData], 4, testCases);
        validateBatchMeta(txn[jss::metaData], preAlice, seq);

        BEAST_EXPECT(env.seq(alice) == 8);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(2) - (feeDrops * 4));
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(2));
    }

    void
    testIndependent(FeatureBitset features)
    {
        testcase("independent");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const seq = env.seq(alice);
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = seq;

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value const tx1 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx1, alice, 1, seq);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice, 2, seq);

        // Tx 3
        Json::Value const tx3 = pay(alice, bob, XRP(999));
        jv = addBatchTx(jv, tx3, alice, 3, seq);

        // Tx 4
        Json::Value const tx4 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx4, alice, 4, seq);

        env(jv, fee(feeDrops * 4), txflags(tfIndependent), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "Payment",
             "CF28B462454DC1651D1705E3C2BD49E0C4D91245C68D3A10D27CF56E5C9B5BE"
             "5"},
            {"tesSUCCESS",
             "Payment",
             "002C79A3D4BB339E09C358450D96B885C21B7F5701B0E908DAC3DFE6C13607D"
             "A"},
            {"tecUNFUNDED_PAYMENT",
             "Payment",
             "E6FC37AF2B22F398E7D32B89C73D2443DF8BE7A2F35CA8B0B6AF6E9A504A67F"
             "4"},
            {"tesSUCCESS",
             "Payment",
             "19E953305CF8D48C481ED35A577196432463AE420D52D68463BD5724492C7E9"
             "6"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 4);
        validateBatchTxns(txn[jss::metaData], 5, testCases);
        validateBatchMeta(txn[jss::metaData], preAlice, seq);

        BEAST_EXPECT(env.seq(alice) == 9);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - (feeDrops * 4));
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
    }

    void
    testMultiParty(FeatureBitset features)
    {
        testcase("multi party");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);
        env.close();

        env(noop(bob), ter(tesSUCCESS));
        env.close();

        auto const seq = env.seq(alice);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        std::vector<TestSignData> const signers = {{
            {0, bob},
        }};

        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = seq;
        auto const batchFee = ((signers.size() + 2) * feeDrops) + feeDrops * 2;
        jv[jss::Fee] = to_string(batchFee);
        jv[jss::Flags] = tfAllOrNothing;
        jv[jss::SigningPubKey] = strHex(alice.pk());

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value tx1 = pay(alice, bob, XRP(10));
        jv = addBatchTx(jv, tx1, alice, 1, seq);

        // Tx 2
        Json::Value const tx2 = pay(bob, alice, XRP(5));
        jv = addBatchTx(jv, tx2, alice, 0, env.seq(bob));

        jv = addBatchSignatures(jv, signers);

        // env(jv, bsig(alice, bob), ter(tesSUCCESS));
        env(jv, ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "Payment",
             "F74D7914EB0CB080E7004AA11AAFCA7687558F1B43C0B34A0438BBC9AE70857"
             "1"},
            {"tesSUCCESS",
             "Payment",
             "9464BBDA1E5893486507DDA75D702739B9FE3DA94D9D002A2DBD3840688AF76"
             "6"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 2);
        validateBatchTxns(txn[jss::metaData], 3, testCases);
        validateBatchMeta(txn[jss::metaData], preAlice, seq);

        BEAST_EXPECT(env.seq(alice) == 6);
        BEAST_EXPECT(env.seq(bob) == 6);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(5) - (batchFee));
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(5));
    }

    void
    testMultisign(FeatureBitset features)
    {
        testcase("multisign");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        env(signers(alice, 2, {{bob, 1}, {carol, 1}}));
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const seq = env.seq(alice);
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = seq;

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value const tx1 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx1, alice, 1, seq);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice, 2, seq);

        env(jv,
            fee(feeDrops * 2 + (feeDrops * 4)),
            txflags(tfAllOrNothing),
            msig(bob, carol),
            ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "Payment",
             "21131DBC8CD39D1A514939F988B56235F33A38BD58762CE0CAF8EFA9489DB32"
             "7"},
            {"tesSUCCESS",
             "Payment",
             "1C25CCB1FF8A57B53B39B9287BA48DCD62DF3F213D125FF22C8A891FAC955C3"
             "2"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 2);
        validateBatchTxns(txn[jss::metaData], 3, testCases);
        validateBatchMeta(txn[jss::metaData], preAlice, seq);

        BEAST_EXPECT(env.seq(alice) == 8);
        BEAST_EXPECT(
            env.balance(alice) ==
            preAlice - XRP(2) - (feeDrops * 2 + (feeDrops * 4)));
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(2));
    }

    void
    testMultisignMultiParty(FeatureBitset features)
    {
        testcase("multisign multi party");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const dave = Account("dave");

        env.fund(XRP(1000), alice, bob, carol, dave);
        env.close();

        env(signers(bob, 2, {{carol, 1}, {dave, 1}}));
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        std::vector<TestSignData> const signers = {{
            {0, dave},
            {0, carol},
        }};

        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = env.seq(alice);
        auto const batchFee = ((signers.size() + 2) * feeDrops) + feeDrops * 2;
        jv[jss::Fee] = to_string(batchFee);
        jv[jss::Flags] = tfAllOrNothing;
        jv[jss::SigningPubKey] = strHex(alice.pk());

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value tx1 = pay(alice, bob, XRP(10));
        jv = addBatchTx(jv, tx1, alice, 1, env.seq(alice));

        // Tx 2
        Json::Value const tx2 = pay(bob, alice, XRP(5));
        jv = addBatchTx(jv, tx2, alice, 0, env.seq(bob));

        jv = addBatchMultiSignatures(jv, 0, bob, signers);

        // env(jv, bsig(alice, bob), ter(tesSUCCESS));
        env(jv, ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "Payment",
             "F74D7914EB0CB080E7004AA11AAFCA7687558F1B43C0B34A0438BBC9AE70857"
             "1"},
            {"tesSUCCESS",
             "Payment",
             "9464BBDA1E5893486507DDA75D702739B9FE3DA94D9D002A2DBD3840688AF76"
             "6"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 2);
        validateBatchTxns(txn[jss::metaData], 3, testCases);
        validateBatchMeta(txn[jss::metaData], STAmount(XRP(1000)), 4);

        BEAST_EXPECT(env.seq(alice) == 6);
        BEAST_EXPECT(env.seq(bob) == 6);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(5) - (batchFee));
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(5));
    }

    void
    testSubmit(FeatureBitset features)
    {
        testcase("submit");

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

        {
            auto jv = pay(alice, bob, USD(1));
            jv[sfBatchTxn.jsonName] = Json::Value{};
            jv[sfBatchTxn.jsonName][jss::OuterAccount] = alice.human();
            jv[sfBatchTxn.jsonName][sfSequence.jsonName] = 0;
            jv[sfBatchTxn.jsonName][sfBatchIndex.jsonName] = 0;

            Serializer s;
            auto jt = env.jt(jv);
            jv.removeMember(sfTxnSignature.jsonName);
            s.erase();
            jt.stx->add(s);
            auto const jrr = env.rpc("submit", strHex(s.slice()))[jss::result];
            // std::cout << jrr << std::endl;
            BEAST_EXPECT(
                jrr[jss::status] == "error" &&
                jrr[jss::error] == "invalidTransaction");

            env.close();
        }
        {
            std::string txBlob =
                "1200002280000000240000000561D4838D7EA4C68000000000000000000000"
                "0000005553440000000000A407AF5856CCF3C42619DAA925813FC955C72983"
                "68400000000000000A73210388935426E0D08083314842EDFBB2D517BD4769"
                "9F9A4527318A8E10468C97C0528114AE123A8556F3CF91154711376AFB0F89"
                "4F832B3D8314F51DFC2A09D62CBBA1DFBDD4691DAC96AD98B90FE023240000"
                "0000801814AE123A8556F3CF91154711376AFB0F894F832B3D00101400E1";
            auto const jrr = env.rpc("submit", txBlob)[jss::result];
            // std::cout << jrr << std::endl;
            BEAST_EXPECT(
                jrr[jss::status] == "success" &&
                jrr[jss::engine_result] == "temINVALID_BATCH");

            env.close();
        }
    }

    void
    testAccountSet(FeatureBitset features)
    {
        testcase("account set");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const seq = env.seq(alice);
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = seq;

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value tx1 = noop(alice);
        std::string const domain = "example.com";
        tx1[sfDomain.fieldName] = strHex(domain);
        jv = addBatchTx(jv, tx1, alice, 1, seq);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice, 2, seq);

        env(jv, fee(feeDrops * 2), txflags(tfAllOrNothing), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "AccountSet",
             "6B6B225E26F2F4811A651D7FD1E4F675D3E9F677C0167F8AAE707E2CB9B508A"
             "6"},
            {"tesSUCCESS",
             "Payment",
             "002C79A3D4BB339E09C358450D96B885C21B7F5701B0E908DAC3DFE6C13607D"
             "A"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 2);
        validateBatchTxns(txn[jss::metaData], 3, testCases);
        validateBatchMeta(txn[jss::metaData], preAlice, seq);

        auto const sle = env.le(keylet::account(alice));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(
            sle->getFieldVL(sfDomain) == Blob(domain.begin(), domain.end()));

        BEAST_EXPECT(env.seq(alice) == 7);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1) - (feeDrops * 2));
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(1));
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

        auto const feeDrops = env.current()->fees().base;
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
        auto const preAliceUSD = env.balance(alice, USD.issue());
        auto const preBobUSD = env.balance(bob, USD.issue());

        std::vector<TestSignData> const signers = {{
            {0, bob},
        }};

        auto const seq = env.seq(alice);
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = seq;
        auto const batchFee = ((signers.size() + 2) * feeDrops) + feeDrops * 2;
        jv[jss::Fee] = to_string(batchFee);
        jv[jss::Flags] = tfAllOrNothing;
        jv[jss::SigningPubKey] = strHex(alice.pk());

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        uint256 const chkId{getCheckIndex(bob, env.seq(bob))};
        Json::Value tx1 = check::create(bob, alice, USD(10));
        jv = addBatchTx(jv, tx1, alice, 0, env.seq(bob));

        // Tx 2
        Json::Value const tx2 = check::cash(alice, chkId, USD(10));
        jv = addBatchTx(jv, tx2, alice, 1, env.seq(alice));

        jv = addBatchSignatures(jv, signers);

        env(jv, ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "CheckCreate",
             "6443C49FA0E30F09AD6EF418EABC031E70AE854D62D0543F34214A3F1BADB5C"
             "1"},
            {"tesSUCCESS",
             "CheckCash",
             "ABFC2892253C19A9312F5BEF9BDA7399498A9650F3F64777EE5A5C498B6BCFB"
             "6"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 2);
        validateBatchTxns(txn[jss::metaData], 3, testCases);
        validateBatchMeta(txn[jss::metaData], preAlice, seq);

        BEAST_EXPECT(env.seq(alice) == 7);
        BEAST_EXPECT(env.seq(bob) == 6);
        BEAST_EXPECT(env.balance(alice) == preAlice - (batchFee));
        BEAST_EXPECT(env.balance(bob) == preBob);
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

        auto const feeDrops = env.current()->fees().base;
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

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        auto const preAliceUSD = env.balance(alice, USD.issue());
        auto const preBobUSD = env.balance(bob, USD.issue());

        std::vector<TestSignData> const signers = {{
            {0, bob},
        }};

        auto const seq = env.seq(alice);
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = seq;
        auto const batchFee = ((signers.size() + 2) * feeDrops) + feeDrops * 2;
        jv[jss::Fee] = to_string(batchFee);
        jv[jss::Flags] = tfAllOrNothing;
        jv[jss::SigningPubKey] = strHex(alice.pk());

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        uint256 const chkId{getCheckIndex(bob, bobTicketSeq)};
        Json::Value tx1 = check::create(bob, alice, USD(10));
        jv = addBatchTx(jv, tx1, alice, 0, 0, bobTicketSeq);

        // Tx 2
        Json::Value const tx2 = check::cash(alice, chkId, USD(10));
        jv = addBatchTx(jv, tx2, alice, 1, env.seq(alice));

        jv = addBatchSignatures(jv, signers);

        env(jv, ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "CheckCreate",
             "3D06827C2B17BAA07887C8E101DC6779EC7A3807E79E88D64022BA14DC0B252"
             "C"},
            {"tesSUCCESS",
             "CheckCash",
             "B276687A136BD0FFE4B03F84ABB5C5F7A72C3D015968CEE83A27A7881E87127"
             "F"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 2);
        validateBatchTxns(txn[jss::metaData], 3, testCases);
        validateBatchMeta(txn[jss::metaData], preAlice, seq);

        BEAST_EXPECT(env.seq(alice) == 7);
        BEAST_EXPECT(env.seq(bob) == 16);
        BEAST_EXPECT(env.balance(alice) == preAlice - (batchFee));
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
        // Env env{*this, envconfig(), features, nullptr,
        //     beast::severities::kTrace
        // };

        auto const feeDrops = env.current()->fees().base;
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

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        auto const preCarol = env.balance(carol);
        auto const preAliceUSD = env.balance(alice, USD.issue());
        auto const preBobUSD = env.balance(bob, USD.issue());

        std::vector<TestSignData> const signers = {{
            {0, bob},
            {1, alice},
        }};

        auto const seq = env.seq(carol);
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = carol.human();
        jv[jss::Sequence] = seq;
        auto const batchFee = ((signers.size() + 2) * feeDrops) + feeDrops * 2;
        jv[jss::Fee] = to_string(batchFee);
        jv[jss::Flags] = tfAllOrNothing;
        jv[jss::SigningPubKey] = strHex(carol.pk());

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        uint256 const chkId{getCheckIndex(bob, env.seq(bob))};
        Json::Value tx1 = check::create(bob, alice, USD(10));
        jv = addBatchTx(jv, tx1, carol, 0, env.seq(bob));

        // Tx 2
        Json::Value const tx2 = check::cash(alice, chkId, USD(10));
        jv = addBatchTx(jv, tx2, carol, 0, env.seq(alice));

        jv = addBatchSignatures(jv, signers);

        env(jv, ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "CheckCreate",
             "74EE0A770F85E93055072F4BD8286D235AE6B333AF41C7AA44A45DD63643752"
             "E"},
            {"tesSUCCESS",
             "CheckCash",
             "9CFCBABC4729B388C265A45F5B6C13ED2AF67942EC21FE6064FDBBF5F131609"
             "3"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 2);
        validateBatchTxns(txn[jss::metaData], 3, testCases);
        validateBatchMeta(txn[jss::metaData], preCarol, seq);

        BEAST_EXPECT(env.seq(alice) == 6);
        BEAST_EXPECT(env.seq(bob) == 6);
        BEAST_EXPECT(env.seq(carol) == 5);
        BEAST_EXPECT(env.balance(alice) == preAlice);
        BEAST_EXPECT(env.balance(bob) == preBob);
        BEAST_EXPECT(env.balance(carol) == preCarol - (batchFee));
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

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);
        env.close();

        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 10));
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const seq = env.seq(alice);
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value const tx1 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx1, alice, 0, seq);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice, 1, seq);

        env(jv,
            fee(feeDrops * 2),
            txflags(tfAllOrNothing),
            ticket::use(aliceTicketSeq++),
            ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "Payment",
             "18629D496965A11CAB4454B86DB794BA07DABA3EE154DEFE9259977221E937E"
             "5"},
            {"tesSUCCESS",
             "Payment",
             "7654B768E091768EB0D43C8EE33B7E72C82BC7A584D578F2646721F69AEEAB7"
             "2"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 2);
        validateBatchTxns(txn[jss::metaData], 3, testCases);
        validateBatchMeta(txn[jss::metaData], preAlice, seq, 10, 10);

        auto const sle = env.le(keylet::account(alice));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getFieldU32(sfOwnerCount) == 9);
        BEAST_EXPECT(sle->getFieldU32(sfTicketCount) == 9);

        BEAST_EXPECT(env.seq(alice) == 17);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(2) - (feeDrops * 2));
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(2));
    }

    void
    testTicketsInner(FeatureBitset features)
    {
        testcase("tickets inner");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);
        env.close();

        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 10));
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const seq = env.seq(alice);
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = seq;

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value const tx1 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx1, alice, 0, 0, aliceTicketSeq);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice, 1, 0, aliceTicketSeq);

        env(jv, fee(feeDrops * 2), txflags(tfAllOrNothing), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "Payment",
             "684C0FE631535577FE8BE663848AB3AFE71C6CD688101E4FEB43B9C13374DBB"
             "2"},
            {"tesSUCCESS",
             "Payment",
             "CBF12A852B0418FAF406C480BE991CE1EA2D0F16323412BFFA9F89CA7449B21"
             "E"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 2);
        validateBatchTxns(txn[jss::metaData], 3, testCases);
        validateBatchMeta(txn[jss::metaData], preAlice, seq, 10, 10);

        auto const sle = env.le(keylet::account(alice));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getFieldU32(sfOwnerCount) == 8);
        BEAST_EXPECT(sle->getFieldU32(sfTicketCount) == 8);

        BEAST_EXPECT(env.seq(alice) == 16);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(2) - (feeDrops * 2));
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(2));
    }

    void
    testTicketsOuterInner(FeatureBitset features)
    {
        testcase("tickets outer inner");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);
        env.close();

        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 10));
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const seq = env.seq(alice);
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value const tx1 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx1, alice, 1, 0, aliceTicketSeq);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice, 0, seq);

        env(jv,
            fee(feeDrops * 2),
            txflags(tfAllOrNothing),
            ticket::use(aliceTicketSeq++),
            ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "Payment",
             "CBF12A852B0418FAF406C480BE991CE1EA2D0F16323412BFFA9F89CA7449B21"
             "E"},
            {"tesSUCCESS",
             "Payment",
             "18629D496965A11CAB4454B86DB794BA07DABA3EE154DEFE9259977221E937E"
             "5"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 2);
        validateBatchTxns(txn[jss::metaData], 3, testCases);
        validateBatchMeta(txn[jss::metaData], preAlice, seq, 10, 10);

        auto const sle = env.le(keylet::account(alice));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getFieldU32(sfOwnerCount) == 8);
        BEAST_EXPECT(sle->getFieldU32(sfTicketCount) == 8);

        BEAST_EXPECT(env.seq(alice) == 16);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(2) - (feeDrops * 2));
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(2));
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testEnable(features);
        testPreflight(features);
        testBadSequence(features);
        testBadFee(features);
        testOutOfSequence(features);
        testAllOrNothing(features);
        testOnlyOne(features);
        testUntilFailure(features);
        testIndependent(features);
        testMultiParty(features);
        testMultisign(features);
        testMultisignMultiParty(features);
        testSubmit(features);
        testAccountSet(features);
        testObjectCreateSequence(features);
        testObjectCreateTicket(features);
        testObjectCreate3rdParty(features);
        testTicketsOuter(features);
        testTicketsInner(features);
        testTicketsOuterInner(features);

        // TODO: previousFields repeat `sfOwnerCount` even if there was no
        // update to `OwnerCount`
        // TODO: PreviousTxnID on the outer batch txn is last inner batch txn?
        // TODO: tecINVARIANT_FAILED
        // You cannot check the invariants without applying the transactions but
        // you cannot revert the transactions after they have been applied.
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