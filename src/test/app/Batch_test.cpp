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
        std::vector<TestBatchData> const& batchResults)
    {
        BEAST_EXPECT(meta[sfBatchExecutions.jsonName].size() > 0);
        size_t index = 0;
        for (auto const& _batchTxn : meta[sfBatchExecutions.jsonName])
        {
            auto const b = _batchTxn[sfBatchExecution.jsonName];
            BEAST_EXPECT(
                b[sfTransactionResult.jsonName] == batchResults[index].result);
            BEAST_EXPECT(
                b[sfTransactionType.jsonName] == batchResults[index].txType);
            if (batchResults[index].hash != "")
                BEAST_EXPECT(
                    b[sfTransactionHash.jsonName] == batchResults[index].hash);
            ++index;
        }
    }

    Json::Value
    addBatchTx(
        Json::Value jv,
        Json::Value const& tx,
        PublicKey const& pk,
        jtx::Account const& account,
        std::uint8_t innerIndex,
        std::uint32_t outerSequence,
        std::uint8_t index)
    {
        jv[sfRawTransactions.jsonName][index] = Json::Value{};
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction] = tx;
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction]
          [jss::SigningPubKey] = strHex(pk);
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction]
          [sfFee.jsonName] = 0;
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction]
          [jss::Sequence] = 0;
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction]
          [sfBatchTxn.jsonName] = Json::Value{};
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction]
          [sfBatchTxn.jsonName][jss::OuterAccount] = account.human();
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction]
          [sfBatchTxn.jsonName][sfSequence.jsonName] = outerSequence;
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction]
          [sfBatchTxn.jsonName][sfBatchIndex.jsonName] = innerIndex;
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

            auto const seq = env.seq("alice");
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            // Batch Transactions
            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            // Tx 1
            Json::Value const tx1 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, seq, 0);

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
            auto const seq = env.seq("alice");
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
            auto const seq = env.seq("alice");
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
            auto const seq = env.seq("alice");
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};
            for (std::uint8_t i = 0; i < 13; ++i)
            {
                Json::Value const tx = pay(alice, bob, XRP(1));
                jv = addBatchTx(jv, tx, alice.pk(), alice, i, seq, i);
            }

            env(jv, ter(temMALFORMED));
            env.close();
        }

        // temBAD_SIGNATURE: Batch: invalid batch txn signature.
        {
            std::vector<TestSignData> const signers = {{
                {0, alice},
                {1, carol},
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
            jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, env.seq(alice), 0);

            // Tx 2
            Json::Value const tx2 = pay(bob, alice, XRP(5));
            jv = addBatchTx(jv, tx2, bob.pk(), alice, 0, env.seq(bob), 1);

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

        // // temMALFORMED: Batch: TransactionType missing in array entry.
        // {
        //     auto const seq = env.seq("alice");
        //     Json::Value jv;
        //     jv[jss::TransactionType] = jss::Batch;
        //     jv[jss::Account] = alice.human();
        //     jv[jss::Sequence] = seq;

        //     jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        //     // Tx 1
        //     Json::Value tx1 = pay(alice, bob, XRP(10));
        //     jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, seq, 0);
        //     jv[sfRawTransactions.jsonName][0u][jss::RawTransaction].removeMember(
        //         jss::TransactionType);

        //     env(jv, ter(temMALFORMED));
        //     env.close();
        // }

        // temMALFORMED: Batch: batch cannot have inner batch txn.
        {
            auto const seq = env.seq("alice");
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
                btx = addBatchTx(btx, btx1, alice.pk(), alice, 0, seq, 0);
            }

            jv = addBatchTx(jv, btx, alice.pk(), alice, 0, seq, 0);

            // Tx 2
            Json::Value const tx2 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx2, alice.pk(), alice, 1, seq, 1);

            env(jv, ter(temMALFORMED));
            env.close();
        }

        // temBAD_SIGNER: Batch: inner txn not signed by the right user.
        {
            std::vector<TestSignData> const signers = {{
                {0, alice},
                {1, carol},
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
            jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, env.seq(alice), 0);

            // Tx 2
            Json::Value const tx2 = pay(bob, alice, XRP(5));
            jv = addBatchTx(jv, tx2, bob.pk(), alice, 0, env.seq(bob), 1);

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
            {0, alice},
            {1, bob},
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
        jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, preAliceSeq, 0);

        // Tx 2
        Json::Value const tx2 = pay(bob, alice, USD(5));
        jv = addBatchTx(jv, tx2, bob.pk(), alice, 10, preBobSeq, 1);

        jv = addBatchSignatures(jv, signers);

        env(jv, ter(terPRE_SEQ));
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
            {0, alice},
            {1, bob},
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
        jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, env.seq(alice), 0);

        // Tx 2
        Json::Value const tx2 = pay(bob, alice, USD(5));
        jv = addBatchTx(jv, tx2, bob.pk(), alice, 0, env.seq(bob), 1);

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
            auto const seq = env.seq("alice");
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            // Batch Transactions
            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            // Tx 1
            Json::Value const tx1 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, seq, 0);

            // Tx 2
            Json::Value const tx2 =
                jtx::trust(alice, alice["USD"](1000), tfSetfAuth);
            jv = addBatchTx(jv, tx2, alice.pk(), alice, 1, seq, 1);

            // Tx 3
            Json::Value const tx3 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx3, alice.pk(), alice, 2, seq, 2);

            env(jv,
                fee(feeDrops * 3),
                txflags(tfAllOrNothing),
                ter(tefNO_AUTH_REQUIRED));
            env.close();
        }

        // tfUntilFailure
        {
            auto const seq = env.seq("alice");
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            // Batch Transactions
            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            // Tx 1
            Json::Value const tx1 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, seq, 0);

            // Tx 2
            Json::Value const tx2 =
                jtx::trust(alice, alice["USD"](1000), tfSetfAuth);
            jv = addBatchTx(jv, tx2, alice.pk(), alice, 1, seq, 1);

            // Tx 3
            Json::Value const tx3 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx3, alice.pk(), alice, 2, seq, 2);

            env(jv,
                fee(feeDrops * 3),
                txflags(tfUntilFailure),
                ter(tefNO_AUTH_REQUIRED));
            env.close();
        }

        // tfOnlyOne
        {
            auto const seq = env.seq("alice");
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            // Batch Transactions
            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            // Tx 1
            Json::Value const tx1 =
                jtx::trust(alice, alice["USD"](1000), tfSetfAuth);
            jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, seq, 0);

            // Tx 2
            Json::Value const tx2 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx2, alice.pk(), alice, 1, seq, 1);

            env(jv,
                fee(feeDrops * 2),
                txflags(tfOnlyOne),
                ter(tefNO_AUTH_REQUIRED));
            env.close();
        }

        // tfIndependent
        {
            auto const seq = env.seq("alice");
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            // Batch Transactions
            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            // Tx 1
            Json::Value const tx1 =
                jtx::trust(alice, alice["USD"](1000), tfSetfAuth);
            jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, seq, 0);

            // Tx 2
            Json::Value const tx2 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx2, alice.pk(), alice, 1, seq, 1);

            env(jv,
                fee(feeDrops * 3),
                txflags(tfIndependent),
                ter(tefNO_AUTH_REQUIRED));
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

            auto const seq = env.seq("alice");
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            // Batch Transactions
            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            // Tx 1
            Json::Value const tx1 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, seq, 0);

            // Tx 2
            Json::Value const tx2 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx2, alice.pk(), alice, 1, seq, 1);

            env(jv,
                fee(feeDrops * 2),
                txflags(tfAllOrNothing),
                ter(tesSUCCESS));
            env.close();

            std::vector<TestBatchData> testCases = {{
                {"tesSUCCESS",
                 "Payment",
                 "3664A012DA8936D0ECF7B84E7447DB88088A2DA9FD61A7BDC4C4DB0CD9944"
                 "3AC"},
                {"tesSUCCESS",
                 "Payment",
                 "44B76513FE9A57E84B837139C1D83A81EB70C88842EC85A561A71F05DF514"
                 "273"},
            }};

            Json::Value params;
            params[jss::ledger_index] = env.current()->seq() - 1;
            params[jss::transactions] = true;
            params[jss::expand] = true;
            auto const jrr = env.rpc("json", "ledger", to_string(params));
            // std::cout << jrr << std::endl;
            auto const txn = getTxByIndex(jrr, 2);
            validateBatchTxns(txn[jss::metaData], testCases);

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

            auto const seq = env.seq("alice");
            Json::Value jv;
            jv[jss::TransactionType] = jss::Batch;
            jv[jss::Account] = alice.human();
            jv[jss::Sequence] = seq;

            // Batch Transactions
            jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

            // Tx 1
            Json::Value const tx1 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, seq, 0);

            // Tx 2
            Json::Value const tx2 = pay(alice, bob, XRP(999));
            jv = addBatchTx(jv, tx2, alice.pk(), alice, 1, seq, 1);

            env(jv,
                fee(feeDrops * 2),
                txflags(tfAllOrNothing),
                ter(tecBATCH_FAILURE));
            env.close();

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

        auto const seq = env.seq("alice");
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = seq;

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 2
        Json::Value const tx1 = pay(alice, bob, XRP(999));
        jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, seq, 0);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice.pk(), alice, 1, seq, 1);

        // Tx 3
        Json::Value const tx3 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx3, alice.pk(), alice, 2, seq, 2);

        env(jv, fee(feeDrops * 3), txflags(tfOnlyOne), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tecUNFUNDED_PAYMENT",
             "Payment",
             "3ACCF055EB1E32947E86B9105E1E71CF6E1DE65D42273DBF006D3AB165DF8B4"
             "2"},
            {"tesSUCCESS",
             "Payment",
             "44B76513FE9A57E84B837139C1D83A81EB70C88842EC85A561A71F05DF51427"
             "3"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 2);
        validateBatchTxns(txn[jss::metaData], testCases);

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

        auto const seq = env.seq("alice");
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = seq;

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value const tx1 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, seq, 0);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice.pk(), alice, 1, seq, 1);

        // Tx 3
        Json::Value const tx3 = pay(alice, bob, XRP(999));
        jv = addBatchTx(jv, tx3, alice.pk(), alice, 2, seq, 2);

        // Tx 4
        Json::Value const tx4 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx4, alice.pk(), alice, 3, seq, 3);

        env(jv, fee(feeDrops * 4), txflags(tfUntilFailure), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "Payment",
             "3664A012DA8936D0ECF7B84E7447DB88088A2DA9FD61A7BDC4C4DB0CD99443A"
             "C"},
            {"tesSUCCESS",
             "Payment",
             "44B76513FE9A57E84B837139C1D83A81EB70C88842EC85A561A71F05DF51427"
             "3"},
            {"tecUNFUNDED_PAYMENT",
             "Payment",
             "ECBA4387E1C76823E0E3EFA216ECABB991B6B3C175E3D29226086B37241A768"
             "2"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 3);
        validateBatchTxns(txn[jss::metaData], testCases);

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

        auto const seq = env.seq("alice");
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = seq;

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value const tx1 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, seq, 0);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice.pk(), alice, 1, seq, 1);

        // Tx 3
        Json::Value const tx3 = pay(alice, bob, XRP(999));
        jv = addBatchTx(jv, tx3, alice.pk(), alice, 2, seq, 2);

        // Tx 4
        Json::Value const tx4 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx4, alice.pk(), alice, 3, seq, 3);

        env(jv, fee(feeDrops * 4), txflags(tfIndependent), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "Payment",
             "3664A012DA8936D0ECF7B84E7447DB88088A2DA9FD61A7BDC4C4DB0CD99443A"
             "C"},
            {"tesSUCCESS",
             "Payment",
             "44B76513FE9A57E84B837139C1D83A81EB70C88842EC85A561A71F05DF51427"
             "3"},
            {"tecUNFUNDED_PAYMENT",
             "Payment",
             "ECBA4387E1C76823E0E3EFA216ECABB991B6B3C175E3D29226086B37241A768"
             "2"},
            {"tesSUCCESS",
             "Payment",
             "2A7AEB6F8EF57D2E6104104B1FA125ABA5B12A94116E0829E74B54D0394E749"
             "1"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 4);
        validateBatchTxns(txn[jss::metaData], testCases);

        BEAST_EXPECT(env.seq(alice) == 9);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - (feeDrops * 4));
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
    }

    void
    testAtomicSwap(FeatureBitset features)
    {
        testcase("atomic swap");

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

        auto const preAlice = env.balance(alice);
        auto const preAliceUSD = env.balance(alice, USD.issue());
        auto const preBob = env.balance(bob);
        auto const preBobUSD = env.balance(bob, USD.issue());

        std::vector<TestSignData> const signers = {{
            {0, alice},
            {1, bob},
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
        Json::Value tx1 = pay(alice, bob, USD(10));
        jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, env.seq(alice), 0);

        // Tx 2
        Json::Value const tx2 = pay(bob, alice, USD(5));
        jv = addBatchTx(jv, tx2, bob.pk(), alice, 0, env.seq(bob), 1);

        jv = addBatchSignatures(jv, signers);

        // env(jv, bsig(alice, bob), ter(tesSUCCESS));
        env(jv, ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "Payment",
             "99C5E1DEBC039D9BADCDB2B786F79F0A72A6E6EE40386BA7485C94B20E6CF8E"
             "E"},
            {"tesSUCCESS",
             "Payment",
             "C18F28FD9BCEF3EC24FADC20BD25608751AF185B319BDD9EBCEAF2D24200F55"
             "C"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 2);
        validateBatchTxns(txn[jss::metaData], testCases);

        BEAST_EXPECT(env.seq(alice) == 7);
        BEAST_EXPECT(env.seq(bob) == 7);
        BEAST_EXPECT(env.balance(alice) == preAlice - (batchFee));
        BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD - USD(5));
        BEAST_EXPECT(env.balance(bob) == preBob);
        BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD + USD(5));
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

        auto const seq = env.seq("alice");
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = seq;

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value tx1 = fset(alice, asfRequireAuth);
        jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, seq, 0);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice.pk(), alice, 1, seq, 1);

        env(jv, fee(feeDrops * 2), txflags(tfAllOrNothing), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "AccountSet",
             "B9BF25231F9923E1F0AD95BFC8F66EED4E76E3B7C36D23326661CB57D7CF5E1"
             "3"},
            {"tesSUCCESS",
             "Payment",
             "44B76513FE9A57E84B837139C1D83A81EB70C88842EC85A561A71F05DF51427"
             "3"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 2);
        validateBatchTxns(txn[jss::metaData], testCases);

        BEAST_EXPECT(env.seq(alice) == 7);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1) - (feeDrops * 2));
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(1));
    }

    void
    testBatch(FeatureBitset features)
    {
        testcase("batch");

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

        auto const seq = env.seq("alice");
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
            btx = addBatchTx(btx, btx1, alice.pk(), alice, 0, seq, 0);
        }

        jv = addBatchTx(jv, btx, alice.pk(), alice, 0, seq, 0);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice.pk(), alice, 1, seq, 1);

        env(jv, fee(feeDrops * 2), txflags(tfAllOrNothing), ter(temMALFORMED));
        env.close();
    }

    static uint256
    getCheckIndex(AccountID const& account, std::uint32_t uSequence)
    {
        return keylet::check(account, uSequence).key;
    }

    void
    testCheckCreate(FeatureBitset features)
    {
        testcase("check create");

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

        auto const seq = env.seq("alice");
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = seq;

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value tx1 = check::create(alice, bob, XRP(10));
        jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, seq, 0);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice.pk(), alice, 1, seq, 1);

        env(jv, fee(feeDrops * 2), txflags(tfAllOrNothing), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "CheckCreate",
             "92E8D7C221CAF96B70EDE21E5DD3A3126F73474EAB7ABB639A6FAF5E45C7D13"
             "6"},
            {"tesSUCCESS",
             "Payment",
             "44B76513FE9A57E84B837139C1D83A81EB70C88842EC85A561A71F05DF51427"
             "3"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 2);
        validateBatchTxns(txn[jss::metaData], testCases);

        BEAST_EXPECT(env.seq(alice) == 7);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1) - (feeDrops * 2));
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(1));
    }

    void
    testCheckCash(FeatureBitset features)
    {
        testcase("check cash");

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
            {0, alice},
            {1, bob},
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
        uint256 const chkId{getCheckIndex(alice, seq + 1)};
        Json::Value tx1 = check::create(alice, bob, USD(10));
        jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, env.seq(alice), 0);

        // Tx 2
        Json::Value const tx2 = check::cash(bob, chkId, USD(10));
        jv = addBatchTx(jv, tx2, bob.pk(), alice, 0, env.seq(bob), 1);

        jv = addBatchSignatures(jv, signers);

        env(jv, ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "CheckCreate",
             "4C63C1F06AE7429CB29D24E32E395C5CEFAB392730B002AF0597E04FCA3651A"
             "0"},
            {"tesSUCCESS",
             "CheckCash",
             "4923A3865BFA7DDD2F43CB4C15B461505CA0605079CC99DCBBCEFE210E57B17"
             "C"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 2);
        validateBatchTxns(txn[jss::metaData], testCases);

        BEAST_EXPECT(env.seq(alice) == 7);
        BEAST_EXPECT(env.seq(bob) == 6);
        BEAST_EXPECT(env.balance(alice) == preAlice - (batchFee));
        BEAST_EXPECT(env.balance(bob) == preBob);
        BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD - USD(10));
        BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD + USD(10));
    }

    void
    testCheckCancel(FeatureBitset features)
    {
        testcase("check cancel");

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
            {0, alice},
            {1, bob},
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
        uint256 const chkId{getCheckIndex(alice, seq + 1)};
        Json::Value tx1 = check::create(alice, bob, XRP(10));
        jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, env.seq(alice), 0);

        // Tx 2
        Json::Value const tx2 = check::cancel(bob, chkId);
        jv = addBatchTx(jv, tx2, bob.pk(), alice, 0, env.seq(bob), 1);

        jv = addBatchSignatures(jv, signers);

        env(jv, ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "CheckCreate",
             "7EA52BD67C03CE73EB3621491EA66A3DC1E1CA0B3AEBC2A8E56908329A6C28B"
             "1"},
            {"tesSUCCESS",
             "CheckCancel",
             "083804635D4A38BE94D35F4FD900AC4B864294926345594409FD70630AFA963"
             "4"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 2);
        validateBatchTxns(txn[jss::metaData], testCases);

        BEAST_EXPECT(env.seq(alice) == 7);
        BEAST_EXPECT(env.seq(bob) == 6);
        BEAST_EXPECT(env.balance(alice) == preAlice - (batchFee));
        BEAST_EXPECT(env.balance(bob) == preBob);
        BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD);
        BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD);
    }

    void
    testClawback(FeatureBitset features)
    {
        testcase("clawback");

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

        env(fset(gw, asfAllowTrustLineClawback));
        env.close();

        env.trust(USD(1000), alice, bob);
        env(pay(gw, alice, USD(100)));
        env(pay(gw, bob, USD(100)));
        env.close();

        auto const preGw = env.balance(gw);
        auto const preBob = env.balance(bob);

        auto const seq = env.seq(gw);
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = gw.human();
        jv[jss::Sequence] = seq;

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value tx1 = claw(gw, bob["USD"](10));
        jv = addBatchTx(jv, tx1, gw.pk(), gw, 0, seq, 0);

        // Tx 2
        Json::Value const tx2 = pay(gw, bob, XRP(1));
        jv = addBatchTx(jv, tx2, gw.pk(), gw, 1, seq, 1);

        env(jv, fee(feeDrops * 2), txflags(tfAllOrNothing), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "Clawback",
             "838F5265749C559B067F5852B98A2B262AA22A88C556E6A51DD7EF9B842FAB1"
             "5"},
            {"tesSUCCESS",
             "Payment",
             "CDD0AF925A52E2D9C9661FDBDD2CD1856FDA058B5E4C262974F9D90698A2800"
             "0"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 2);
        validateBatchTxns(txn[jss::metaData], testCases);

        BEAST_EXPECT(env.seq(gw) == 10);
        BEAST_EXPECT(env.balance(gw) == preGw - XRP(1) - (feeDrops * 2));
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(1));
    }

    void
    testOfferCancel(FeatureBitset features)
    {
        testcase("offer cancel");

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

        auto const seq = env.seq(alice);
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = seq;

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value tx1 = offer(alice, XRP(50), USD(50));
        jv = addBatchTx(jv, tx1, alice.pk(), alice, 0, seq, 0);

        // Tx 2
        Json::Value const tx2 = offer_cancel(alice, seq + 1);
        jv = addBatchTx(jv, tx2, alice.pk(), alice, 1, seq, 1);

        env(jv, fee(feeDrops * 2), txflags(tfAllOrNothing), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "OfferCreate",
             "145CBCD0C29955E43452EF891979DF63CB9D32CDB6F91FEAEE402D39FC53845"
             "5"},
            {"tesSUCCESS",
             "OfferCancel",
             "10C5B1A9861230AD0BC9BA9FD957944B2A06F5A5C888557586D96EE58DD5861"
             "8"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const txn = getTxByIndex(jrr, 3);
        validateBatchTxns(txn[jss::metaData], testCases);

        BEAST_EXPECT(env.seq(alice) == 7);
        BEAST_EXPECT(env.balance(alice) == preAlice - (feeDrops * 2));
        BEAST_EXPECT(env.balance(bob) == preBob);
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
        testAtomicSwap(features);
        testAccountSet(features);
        testBatch(features);
        testCheckCreate(features);
        testCheckCash(features);
        testCheckCancel(features);
        testClawback(features);
        // testOfferCancel(features);
        testSubmit(features);
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