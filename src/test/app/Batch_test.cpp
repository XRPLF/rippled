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
          [sfBatchTxn.jsonName][jss::Account] = account.human();
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction]
          [sfBatchTxn.jsonName][sfOuterSequence.jsonName] = outerSequence;
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
                 "BC50DFE508E921460443261F46383A68610BB929F78030EA700E373654187851"},
                {"tesSUCCESS",
                 "Payment",
                 "B76CA59CEBEF3B9D64F1ED81BFEA5A6BE4E1A9194F81776916A4A4E4C79BDBAD"},
            }};

            Json::Value params;
            params[jss::ledger_index] = env.current()->seq() - 1;
            params[jss::transactions] = true;
            params[jss::expand] = true;
            auto const jrr = env.rpc("json", "ledger", to_string(params));
            std::cout << jrr << std::endl;
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
            {"tecUNFUNDED_PAYMENT", "Payment", "32B90ABCD36E4601196708F5C93568BA49BE6F1221D58703EAFFB568FAC9807E"},
            {"tesSUCCESS",
             "Payment",
             "B76CA59CEBEF3B9D64F1ED81BFEA5A6BE4E1A9194F81776916A4A4E4C79BDBAD"},
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
             "BC50DFE508E921460443261F46383A68610BB929F78030EA700E37365418785"
             "1"},
            {"tesSUCCESS",
             "Payment",
             "B76CA59CEBEF3B9D64F1ED81BFEA5A6BE4E1A9194F81776916A4A4E4C79BDBA"
             "D"},
            {"tecUNFUNDED_PAYMENT",
             "Payment",
             "5B3B8F9557B38CABE92B8A420C36508711B921B912C391E7029836CD1BAE0BB"
             "6"},
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
             "BC50DFE508E921460443261F46383A68610BB929F78030EA700E373654187851"},
            {"tesSUCCESS",
             "Payment",
             "B76CA59CEBEF3B9D64F1ED81BFEA5A6BE4E1A9194F81776916A4A4E4C79BDBAD"},
            {"tecUNFUNDED_PAYMENT",
             "Payment",
             "5B3B8F9557B38CABE92B8A420C36508711B921B912C391E7029836CD1BAE0BB6"},
            {"tesSUCCESS",
             "Payment",
             "FCB23E17BDCCC55DC354EF8F6D3D1E7E39E705ADF65106D005731736B190A1EC"},
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
        // Env env{*this, envconfig(), features, nullptr,
        //     beast::severities::kTrace
        // };

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
             "319131912A291734CCF2766390B6010E1C63D2916011EE6A154B6F210BE43A72"},
            {"tesSUCCESS",
             "Payment",
             "CBA8ADE2945A4DC41E9E979CE274630E38B606ECB5922366FF77539FE3D01CCD"},
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
             "21D36FEF5EE86F7B4B8609661E138AB166780745F48A94981FB9BBFFF94E780C"},
            {"tesSUCCESS",
             "Payment",
             "B76CA59CEBEF3B9D64F1ED81BFEA5A6BE4E1A9194F81776916A4A4E4C79BDBAD"},
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
             "80229431EBDCF659E1EE1B6E5DC94B195B2E52E81D88663224EC8C06F301FF1F"},
            {"tesSUCCESS",
             "Payment",
             "B76CA59CEBEF3B9D64F1ED81BFEA5A6BE4E1A9194F81776916A4A4E4C79BDBAD"},
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
             "CE920705201AC0E4B3C50E52170BF1ED31E90457CAC4E79A0E1C1AE82BA5CC46"},
            {"tesSUCCESS",
             "CheckCash",
             "165AE829F04BD375DDC3A7F57386B2B2A9386211839C005E1481A4E4282F625F"},
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
             "3C33B38C8804F0FF01BEADE2CFD665C028A53A4220E0D08A73EB7CBBB82D59AC"},
            {"tesSUCCESS",
             "CheckCancel",
             "BFED069E7ECDFD313BDAC1A985C9F05EFD2FBBF63D6C42E8DDEF09087C4D233A"},
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
             "D08330FED53B479F2949DF85717101ED513B046B06B748BDF19F5951A81DAAE2"},
            {"tesSUCCESS",
             "Payment",
             "FEB6D7EE4BE48851B1CE9B31DB39A5A4FDFBB59DFFE1A49146FD6D9177C1ECC6"},
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
        // Env env{*this, envconfig(), features, nullptr,
        //     beast::severities::kTrace
        // };

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
             "145CBCD0C29955E43452EF891979DF63CB9D32CDB6F91FEAEE402D39FC538455"},
            {"tesSUCCESS",
             "OfferCancel",
             "10C5B1A9861230AD0BC9BA9FD957944B2A06F5A5C888557586D96EE58DD58618"},
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

        // test::jtx::Env env{*this, envconfig()};
        Env env{*this, envconfig(), features, nullptr,
            beast::severities::kTrace
        };

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

        auto jv = pay(alice, bob, USD(1));
        jv[sfBatchTxn.jsonName] = Json::Value{};
        jv[sfBatchTxn.jsonName][jss::Account] = alice.human();
        jv[sfBatchTxn.jsonName][sfOuterSequence.jsonName] = 0;
        jv[sfBatchTxn.jsonName][sfBatchIndex.jsonName] = 0;

        Serializer s;
        auto jt = env.jt(jv);
        s.erase();
        jt.stx->add(s);
        auto const jr = env.rpc("submit", strHex(s.slice()));
        BEAST_EXPECT(
            jr.isMember(jss::engine_result) &&
            jr[jss::engine_result] == "tesSUCCESS");
        
        env.close();
        std::cout << jr << std::endl;
    }

    void
    testWithFeats(FeatureBitset features)
    {
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
        
        // testSubmit(features);

        // Test Fork From one node having 1 extra txn

        // Multisign Atomic
        // If the 2nd fails and needs the 3rd
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