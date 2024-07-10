//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#include <ripple/protocol/Feature.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

// tfOnlyOne
// Tx1: Payment = tecUNFUNDED => Leave
// Tx2: Payment = tesSUCCESS => Leave
// TER(tesSUCCESS)

// tfUntilFailure
// Tx1: Payment = tesSUCCESS => Leave
// Tx2: Payment = tecUNFUNDED => Leave
// TER(tesSUCCESS)

// tfBatchAtomic
// Tx1: Payment = tesSUCCESS => Revert
// Tx2: Payment = tecUNFUNDED => Leave
// TER(tecBATCH_FAILURE)

// Broadcast - Manually broadcast the inner transactions
// Stacking Views
// TicketCreate as first of batch
// Sequence optional except when needed specifically
// Bypass the queue for inner transactions
// Fee only on outer - if 2 payments and fee escellation make sure that the outer tx includes the fee escallation for all inner
// Look at TicketCreate for creating virtual tickets (Transactor.cpp) (TransactionConsequences.cpp)
// If we have the batch index, the virtual ticket number is seq of batch + batch index.
// Think about multiple different angles of this. AccountA & AccountB.

// OptionB: Use OfferID

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

    void
    validateBatchTxns(
        Json::Value meta,
        std::vector<TestBatchData> const& batchResults)
    {
        size_t index = 0;
        for (auto const& _batchTxn : meta[sfBatchExecutions.jsonName])
        {
            auto const b = _batchTxn[sfBatchExecution.jsonName];
            BEAST_EXPECT(b[sfTransactionResult.jsonName] == batchResults[index].result);
            BEAST_EXPECT(b[sfTransactionType.jsonName] == batchResults[index].txType);
            if (batchResults[index].hash != "")
                BEAST_EXPECT(b[sfTransactionHash.jsonName] == batchResults[index].hash);
            ++index;
        }
    }
  
    Json::Value
    addBatchTx(
        Json::Value jv,
        Json::Value const& tx,
        jtx::Account const& account,
        XRPAmount feeDrops,
        std::uint8_t index,
        std::uint32_t outerSequence)
    {
        jv[sfRawTransactions.jsonName][index] = Json::Value{};
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction] = tx;
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction][jss::SigningPubKey] = strHex(account.pk());
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction][sfFee.jsonName] = 0;
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction][jss::Sequence] = 0;
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction][sfBatchTxn.jsonName] = Json::Value{};
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction][sfBatchTxn.jsonName][jss::Account] = account.human();
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction][sfBatchTxn.jsonName][sfOuterSequence.jsonName] = outerSequence;
        jv[sfRawTransactions.jsonName][index][jss::RawTransaction][sfBatchTxn.jsonName][sfBatchIndex.jsonName] = index;
        return jv;
    }

    void
    testTemplate(FeatureBitset features)
    {
        testcase("template");

        using namespace test::jtx;
        using namespace std::literals;

        // test::jtx::Env env{*this, envconfig()};
        Env env{
            *this,
            envconfig(),
            features,
            nullptr,
            // beast::severities::kWarning
            beast::severities::kTrace};

        auto const feeDrops = env.current()->fees().base;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        auto const seq = env.seq("alice");
        std::cout << "seq: " << seq << "\n";
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();
        jv[jss::Sequence] = seq;

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value const tx1 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx1, alice, feeDrops, 0, seq);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice, feeDrops, 1, seq);

        env(jv, fee(feeDrops * 2), txflags(tfAllOrNothing), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS", "Payment"},
            {"tesSUCCESS", "Payment"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        std::cout << "jrr: " << jrr << "\n";
        auto const meta = jrr[jss::result][jss::meta];
        validateBatchTxns(meta, testCases);

        std::cout << "seq: " << env.seq(alice) << "\n";
        std::cout << "alice: " << env.balance(alice) << "\n";
        std::cout << "bob: " << env.balance(bob) << "\n";

        BEAST_EXPECT(env.seq(alice) == 7);
        BEAST_EXPECT(env.balance(alice) == XRP(1000) - XRP(2) - (feeDrops * 2));
        BEAST_EXPECT(env.balance(bob) == XRP(1000) + XRP(2));
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
            jv = addBatchTx(jv, tx1, alice, feeDrops, 0, seq);

            // Tx 2
            Json::Value const tx2 = pay(alice, bob, XRP(1));
            jv = addBatchTx(jv, tx2, alice, feeDrops, 1, seq);

            env(jv, fee(feeDrops * 2), txflags(tfAllOrNothing), ter(tesSUCCESS));
            env.close();

            std::vector<TestBatchData> testCases = {{
                {"tesSUCCESS", "Payment", "FE01269C9BABCE17758CEF4DA45BDB529DDA0105FD2360BE00316345637E188D"},
                {"tesSUCCESS", "Payment", "591CF8801EA7B0465DBF309D2B6D103D5E5926203A10F5A433A704C29C1D326F"},
            }};

            Json::Value params;
            params[jss::ledger_index] = env.current()->seq() - 1;
            params[jss::transactions] = true;
            params[jss::expand] = true;
            auto const jrr = env.rpc("json", "ledger", to_string(params));
            std::cout << "jrr: " << jrr << "\n";
            auto const meta = jrr[jss::result][jss::meta];
            validateBatchTxns(meta, testCases);

            BEAST_EXPECT(env.seq(alice) == 7);
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(2) - (feeDrops * 2));
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
            jv = addBatchTx(jv, tx1, alice, feeDrops, 0, seq);

            // Tx 2
            Json::Value const tx2 = pay(alice, bob, XRP(999));
            jv = addBatchTx(jv, tx2, alice, feeDrops, 1, seq);

            env(jv, fee(feeDrops * 2), txflags(tfAllOrNothing), ter(tesSUCCESS));
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
            auto const meta = jrr[jss::result][jss::meta];
            validateBatchTxns(meta, testCases);

            BEAST_EXPECT(env.seq(alice) == 7);
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
        jv = addBatchTx(jv, tx1, alice, feeDrops, 0, seq);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice, feeDrops, 1, seq);

        // Tx 3
        Json::Value const tx3 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx3, alice, feeDrops, 2, seq);

        env(jv, fee(feeDrops * 3), txflags(tfOnlyOne), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tecUNFUNDED_PAYMENT", "Payment", ""},
            {"tesSUCCESS", "Payment", "591CF8801EA7B0465DBF309D2B6D103D5E5926203A10F5A433A704C29C1D326F"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const meta = jrr[jss::result][jss::meta];
        validateBatchTxns(meta, testCases);

        BEAST_EXPECT(env.seq(alice) == 8);
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
        jv = addBatchTx(jv, tx1, alice, feeDrops, 0, seq);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice, feeDrops, 1, seq);

        // Tx 3
        Json::Value const tx3 = pay(alice, bob, XRP(999));
        jv = addBatchTx(jv, tx3, alice, feeDrops, 2, seq);

        // Tx 4
        Json::Value const tx4 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx4, alice, feeDrops, 3, seq);

        env(jv, fee(feeDrops * 4), txflags(tfUntilFailure), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS", "Payment", "FE01269C9BABCE17758CEF4DA45BDB529DDA0105FD2360BE00316345637E188D"},
            {"tesSUCCESS", "Payment", "591CF8801EA7B0465DBF309D2B6D103D5E5926203A10F5A433A704C29C1D326F"},
            {"tecUNFUNDED_PAYMENT", "Payment", ""},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const meta = jrr[jss::result][jss::meta];
        validateBatchTxns(meta, testCases);

        BEAST_EXPECT(env.seq(alice) == 9);
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
        jv = addBatchTx(jv, tx1, alice, feeDrops, 0, seq);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, alice, feeDrops, 1, seq);

        // Tx 3
        Json::Value const tx3 = pay(alice, bob, XRP(999));
        jv = addBatchTx(jv, tx3, alice, feeDrops, 2, seq);

        // Tx 4
        Json::Value const tx4 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx4, alice, feeDrops, 3, seq);

        env(jv, fee(feeDrops * 4), txflags(tfIndependent), ter(tesSUCCESS));
        env.close();

        std::vector<TestBatchData> testCases = {{
            {"tesSUCCESS", "Payment", "FE01269C9BABCE17758CEF4DA45BDB529DDA0105FD2360BE00316345637E188D"},
            {"tesSUCCESS", "Payment", "591CF8801EA7B0465DBF309D2B6D103D5E5926203A10F5A433A704C29C1D326F"},
            {"tecUNFUNDED_PAYMENT", "Payment", ""},
            {"tesSUCCESS", "Payment", "963BCD15F8CC7D6FB3D3154324CDF6CFBEF6A230496676D58DB92109E4A9F1C8"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const meta = jrr[jss::result][jss::meta];
        validateBatchTxns(meta, testCases);

        BEAST_EXPECT(env.seq(alice) == 9);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - (feeDrops * 4));
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
    }

    void
    testWithFeats(FeatureBitset features)
    {
        // testTemplate(features);
        testAllOrNothing(features);
        testOnlyOne(features);
        testUntilFailure(features);
        testIndependent(features);

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
