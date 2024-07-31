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

    void
    validateBatchTxns(
        Json::Value meta,
        std::vector<TestBatchData> const& batchResults)
    {
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
        jtx::Account const& account,
        std::uint8_t innerIndex,
        std::uint32_t outerSequence,
        std::uint8_t index)
    {
        jv[sfRawTransactions.jsonName][index] = Json::Value{};
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction] = tx;
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction]
          [jss::SigningPubKey] = strHex(account.pk());
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
            jv = addBatchTx(jv, tx1, alice, 1, seq, 0);

            // Tx 2
            Json::Value const tx2 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx2, alice, 2, seq, 1);

            env(jv,
                fee(feeDrops * 2),
                txflags(tfAllOrNothing),
                ter(tesSUCCESS));
            env.close();

            std::vector<TestBatchData> testCases = {{
                {"tesSUCCESS",
                 "Payment",
                 "FE01269C9BABCE17758CEF4DA45BDB529DDA0105FD2360BE00316345637E1"
                 "88D"},
                {"tesSUCCESS",
                 "Payment",
                 "591CF8801EA7B0465DBF309D2B6D103D5E5926203A10F5A433A704C29C1D3"
                 "26F"},
            }};

            Json::Value params;
            params[jss::ledger_index] = env.current()->seq() - 1;
            params[jss::transactions] = true;
            params[jss::expand] = true;
            auto const jrr = env.rpc("json", "ledger", to_string(params));
            // std::cout << jrr << std::endl;
            auto const meta = jrr[jss::result][jss::metaData];
            validateBatchTxns(meta, testCases);

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
            jv = addBatchTx(jv, tx1, alice, 1, seq, 0);

            // Tx 2
            Json::Value const tx2 = pay(alice, bob, XRP(999));
            jv = addBatchTx(jv, tx2, alice, 2, seq, 1);

            env(jv,
                fee(feeDrops * 2),
                txflags(tfAllOrNothing),
                ter(tesSUCCESS));
            env.close();

            std::vector<TestBatchData> testCases = {{
                {"tesSUCCESS", "Payment", ""},
                {"tecUNFUNDED_PAYMENT", "Payment", ""},
            }};

            Json::Value params;
            params[jss::ledger_index] = env.current()->seq() - 1;
            params[jss::transactions] = true;
            params[jss::expand] = true;
            auto const jrr = env.rpc("json", "ledger", to_string(params));
            // std::cout << jrr << std::endl;
            auto const meta = jrr[jss::result][jss::metaData];
            validateBatchTxns(meta, testCases);

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
        jv = addBatchTx(jv, tx1, alice, 1, seq, 0);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice, 2, seq, 1);

        // Tx 3
        Json::Value const tx3 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx3, alice, 3, seq, 2);

        env(jv, fee(feeDrops * 3), txflags(tfOnlyOne), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tecUNFUNDED_PAYMENT", "Payment", ""},
            {"tesSUCCESS",
             "Payment",
             "591CF8801EA7B0465DBF309D2B6D103D5E5926203A10F5A433A704C29C1D326"
             "F"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const meta = jrr[jss::result][jss::metaData];
        validateBatchTxns(meta, testCases);

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
        jv = addBatchTx(jv, tx1, alice, 1, seq, 0);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice, 2, seq, 1);

        // Tx 3
        Json::Value const tx3 = pay(alice, bob, XRP(999));
        jv = addBatchTx(jv, tx3, alice, 3, seq, 2);

        // Tx 4
        Json::Value const tx4 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx4, alice, 4, seq, 3);

        env(jv, fee(feeDrops * 4), txflags(tfUntilFailure), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "Payment",
             "FE01269C9BABCE17758CEF4DA45BDB529DDA0105FD2360BE00316345637E188"
             "D"},
            {"tesSUCCESS",
             "Payment",
             "591CF8801EA7B0465DBF309D2B6D103D5E5926203A10F5A433A704C29C1D326"
             "F"},
            {"tecUNFUNDED_PAYMENT", "Payment", ""},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const meta = jrr[jss::result][jss::metaData];
        validateBatchTxns(meta, testCases);

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
        jv = addBatchTx(jv, tx1, alice, 1, seq, 0);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice, 2, seq, 1);

        // Tx 3
        Json::Value const tx3 = pay(alice, bob, XRP(999));
        jv = addBatchTx(jv, tx3, alice, 3, seq, 2);

        // Tx 4
        Json::Value const tx4 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx4, alice, 4, seq, 3);

        env(jv, fee(feeDrops * 4), txflags(tfIndependent), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "Payment",
             "FE01269C9BABCE17758CEF4DA45BDB529DDA0105FD2360BE00316345637E188"
             "D"},
            {"tesSUCCESS",
             "Payment",
             "591CF8801EA7B0465DBF309D2B6D103D5E5926203A10F5A433A704C29C1D326"
             "F"},
            {"tecUNFUNDED_PAYMENT", "Payment", ""},
            {"tesSUCCESS",
             "Payment",
             "963BCD15F8CC7D6FB3D3154324CDF6CFBEF6A230496676D58DB92109E4A9F1C"
             "8"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const meta = jrr[jss::result][jss::metaData];
        validateBatchTxns(meta, testCases);

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
        jv = addBatchTx(jv, tx1, alice, 1, env.seq(alice), 0);

        // Tx 2
        Json::Value const tx2 = pay(bob, alice, USD(5));
        jv = addBatchTx(jv, tx2, bob, 0, env.seq(bob), 1);

        jv = addBatchSignatures(jv, signers);

        // env(jv, bsig(alice, bob), ter(tesSUCCESS));
        env(jv, ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "Payment",
             "319131912A291734CCF2766390B6010E1C63D2916011EE6A154B6F210BE43A7"
             "2"},
            {"tesSUCCESS",
             "Payment",
             "DF91E311E37F7670DBB31E98AB6C309555B5B0B20A1DBADFAB2BAC8E4DC8E27"
             "0"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const meta = jrr[jss::result][jss::metaData];
        validateBatchTxns(meta, testCases);

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
        jv = addBatchTx(jv, tx1, alice, 1, seq, 0);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice, 2, seq, 1);

        env(jv, fee(feeDrops * 2), txflags(tfAllOrNothing), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "AccountSet",
             "26F8C5399D4F40DEC5051F57CFBCE27F4A6EB3E013332C05748E7C5450FE074"
             "4"},
            {"tesSUCCESS",
             "Payment",
             "591CF8801EA7B0465DBF309D2B6D103D5E5926203A10F5A433A704C29C1D3"
             "26F"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const meta = jrr[jss::result][jss::metaData];
        validateBatchTxns(meta, testCases);

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
            btx = addBatchTx(btx, btx1, alice, 0, seq, 0);
        }

        jv = addBatchTx(jv, btx, alice, 1, seq, 0);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice, 2, seq, 1);

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
        jv = addBatchTx(jv, tx1, alice, 0, seq, 0);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice, 1, seq, 1);

        env(jv, fee(feeDrops * 2), txflags(tfAllOrNothing), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "CheckCreate",
             "26F8C5399D4F40DEC5051F57CFBCE27F4A6EB3E013332C05748E7C5450FE074"
             "4"},
            {"tesSUCCESS",
             "Payment",
             "591CF8801EA7B0465DBF309D2B6D103D5E5926203A10F5A433A704C29C1D3"
             "26F"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const meta = jrr[jss::result][jss::metaData];
        validateBatchTxns(meta, testCases);

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
        jv = addBatchTx(jv, tx1, alice, 1, env.seq(alice), 0);

        // Tx 2
        Json::Value const tx2 = check::cash(bob, chkId, USD(10));
        jv = addBatchTx(jv, tx2, bob, 0, env.seq(bob), 1);

        jv = addBatchSignatures(jv, signers);

        env(jv, ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "CheckCreate",
             "145CBCD0C29955E43452EF891979DF63CB9D32CDB6F91FEAEE402D39FC53845"
             "5"},
            {"tesSUCCESS",
             "CheckCash",
             "10C5B1A9861230AD0BC9BA9FD957944B2A06F5A5C888557586D96EE58DD5861"
             "8"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const meta = jrr[jss::result][jss::metaData];
        validateBatchTxns(meta, testCases);

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
        jv = addBatchTx(jv, tx1, alice, 1, env.seq(alice), 0);

        // Tx 2
        Json::Value const tx2 = check::cancel(bob, chkId);
        jv = addBatchTx(jv, tx2, bob, 0, env.seq(bob), 1);

        jv = addBatchSignatures(jv, signers);

        env(jv, ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "CheckCreate",
             "145CBCD0C29955E43452EF891979DF63CB9D32CDB6F91FEAEE402D39FC53845"
             "5"},
            {"tesSUCCESS",
             "CheckCancel",
             "10C5B1A9861230AD0BC9BA9FD957944B2A06F5A5C888557586D96EE58DD5861"
             "8"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        // std::cout << jrr << std::endl;
        auto const meta = jrr[jss::result][jss::metaData];
        validateBatchTxns(meta, testCases);

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
        jv = addBatchTx(jv, tx1, gw, 1, seq, 0);

        // Tx 2
        Json::Value const tx2 = pay(gw, bob, XRP(1));
        jv = addBatchTx(jv, tx2, gw, 2, seq, 1);

        env(jv, fee(feeDrops * 2), txflags(tfAllOrNothing), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS",
             "Clawback",
             "D08330FED53B479F2949DF85717101ED513B046B06B748BDF19F5951A81DAAE"
             "2"},
            {"tesSUCCESS",
             "Payment",
             "897B243D48B813D249F8A1353FC3E537DDCC5BD0139CF2670D0FECD435AB1A6"
             "6"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const meta = jrr[jss::result][jss::metaData];
        validateBatchTxns(meta, testCases);

        BEAST_EXPECT(env.seq(gw) == 10);
        BEAST_EXPECT(env.balance(gw) == preGw - XRP(1) - (feeDrops * 2));
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(1));
    }

    void
    testOffer(FeatureBitset features)
    {
        testcase("offer");

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
        jv = addBatchTx(jv, tx1, alice, 0, seq, 0);

        // Tx 2
        Json::Value const tx2 = offer_cancel(alice, seq + 0 + 1);
        jv = addBatchTx(jv, tx2, alice, 1, seq, 1);

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
        auto const meta = jrr[jss::result][jss::ledger][jss::metaData];
        validateBatchTxns(meta, testCases);

        BEAST_EXPECT(env.seq(alice) == 8);
        BEAST_EXPECT(env.balance(alice) == preAlice - (feeDrops * 2));
        BEAST_EXPECT(env.balance(bob) == preBob);
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
        // testOffer(features);

        // Test Fork From one node having 1 extra txn
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