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
    };

    void
    validateBatchTxns(
        Json::Value meta,
        std::array<TestBatchData, 2> batchResults)
    {
        size_t index = 0;
        for (auto const& _batchTxn : meta[sfBatchExecutions.jsonName])
        {
            auto const batchTxn = _batchTxn[sfBatchExecution.jsonName];
            BEAST_EXPECT(batchTxn[sfTransactionResult.jsonName] == batchResults[index].result);
            BEAST_EXPECT(batchTxn[sfTransactionType.jsonName] == batchResults[index].txType);
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

    // OnSucess -> (0)
    // OnFailure -> Next Tx Index
    // Ex.
    // 0: MintURIToken: If Fail -> 2
    // 1: Payment: If Fail -> 2
    // 2: Payment: 0
  
    void
    testBadPubKey(FeatureBitset features)
    {
        testcase("bad pubkey");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};
        // Env env{
        //     *this,
        //     envconfig(),
        //     features,
        //     nullptr,
        //     // beast::severities::kWarning
        //     beast::severities::kTrace};

        auto const feeDrops = env.current()->fees().base;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const seq = env.seq("alice");
        std::cout << "seq: " << seq << "\n";
        // ttBATCH
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value const tx1 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx1, alice, feeDrops, 0, 0);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx2, bob, feeDrops, 1, 0);

        env(jv, fee(feeDrops * 2), ter(tesSUCCESS));
        env.close();

        std::array<TestBatchData, 2> testCases = {{
            {"tesSUCCESS", "Payment"},
            {"tesSUCCESS", "Payment"},
        }};

        Json::Value params;
        params[jss::transaction] = env.tx()->getJson(JsonOptions::none)[jss::hash];
        auto const jrr = env.rpc("json", "tx", to_string(params));
        std::cout << "jrr: " << jrr << "\n";
        auto const meta = jrr[jss::result][jss::meta];
        validateBatchTxns(meta, testCases);

        BEAST_EXPECT(env.seq(alice) == 2);
        BEAST_EXPECT(env.balance(alice) == XRP(1000) - XRP(1) - (feeDrops * 2));
        BEAST_EXPECT(env.balance(bob) == XRP(1000) + XRP(1));
    }
  
    void
    testUnfunded(FeatureBitset features)
    {
        testcase("unfunded");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        auto const seq = env.seq("alice");
        // ttBATCH
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();

        // Batch Transactions
        jv[sfRawTransactions.jsonName] = Json::Value{Json::arrayValue};

        // Tx 1
        Json::Value const tx1 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx1, alice, feeDrops, 0, 0);

        // Tx 2
        Json::Value const tx2 = pay(alice, bob, XRP(999));
        jv = addBatchTx(jv, tx2, alice, feeDrops, 1, 0);

        env(jv, fee(feeDrops * 2), ter(tesSUCCESS));
        env.close();

        std::array<TestBatchData, 2> testCases = {{
            {"tesSUCCESS", "Payment"},
            {"tecUNFUNDED_PAYMENT", "Payment"},
        }};

        Json::Value params;
        params[jss::transaction] = env.tx()->getJson(JsonOptions::none)[jss::hash];
        auto const jrr = env.rpc("json", "tx", to_string(params));
        std::cout << "jrr: " << jrr << "\n";
        auto const meta = jrr[jss::result][jss::meta];
        validateBatchTxns(meta, testCases);

        BEAST_EXPECT(env.seq(alice) == 2);
        BEAST_EXPECT(env.balance(alice) == XRP(1000) - XRP(1) - (feeDrops * 1));
        BEAST_EXPECT(env.balance(bob) == XRP(1000) + XRP(1));
    }

    void
    testSuccess(FeatureBitset features)
    {
        testcase("batch success");

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
        // ttBATCH
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

        std::array<TestBatchData, 2> testCases = {{
            {"tesSUCCESS", "Payment"},
            {"tesSUCCESS", "Payment"},
        }};

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        std::cout << "jrr: " << jrr << "\n";

        // Json::Value params;
        // params[jss::transaction] = env.tx()->getJson(JsonOptions::none)[jss::hash];
        // auto const jrr = env.rpc("json", "tx", to_string(params));
        // std::cout << "jrr: " << jrr << "\n";
        // auto const meta = jrr[jss::result][jss::meta];
        // validateBatchTxns(meta, testCases);

        std::cout << "seq: " << env.seq(alice) << "\n";
        std::cout << "alice: " << env.balance(alice) << "\n";
        std::cout << "bob: " << env.balance(bob) << "\n";

        BEAST_EXPECT(env.seq(alice) == 7);
        BEAST_EXPECT(env.balance(alice) == XRP(1000) - XRP(2) - (feeDrops * 2));
        BEAST_EXPECT(env.balance(bob) == XRP(1000) + XRP(2));
    }

    void
    testAtomicFailure(FeatureBitset features)
    {
        testcase("atomic failure");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};
        auto const feeDrops = env.current()->fees().base;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

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

        env(jv, fee(feeDrops * 2), txflags(tfAllOrNothing), ter(tecBATCH_FAILURE));
        env.close();

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        std::cout << "jrr: " << jrr << "\n";

        std::cout << "seq: " << env.seq(alice) << "\n";
        std::cout << "alice: " << env.balance(alice) << "\n";
        std::cout << "bob: " << env.balance(bob) << "\n";

        BEAST_EXPECT(env.seq(alice) == 5);
        BEAST_EXPECT(env.balance(alice) == XRP(1000) - (feeDrops * 2));
        BEAST_EXPECT(env.balance(bob) == XRP(1000));
    }

    void
    testFirstFailure(FeatureBitset features)
    {
        testcase("first failure");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};
        // Env env{
        //     *this,
        //     envconfig(),
        //     features,
        //     nullptr,
        //     // beast::severities::kWarning
        //     beast::severities::kTrace};
        auto const feeDrops = env.current()->fees().base;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

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

        // Tx 3
        Json::Value const tx3 = pay(alice, bob, XRP(1));
        jv = addBatchTx(jv, tx3, alice, feeDrops, 2, seq);

        env(jv, fee(feeDrops * 3), txflags(tfOnlyOne), ter(tecBATCH_FAILURE));
        env.close();

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        std::cout << "jrr: " << jrr << "\n";

        std::cout << "seq: " << env.seq(alice) << "\n";
        std::cout << "alice: " << env.balance(alice) << "\n";
        std::cout << "bob: " << env.balance(bob) << "\n";

        BEAST_EXPECT(env.seq(alice) == 4);
        BEAST_EXPECT(env.balance(alice) == XRP(1000) - XRP(1));
        BEAST_EXPECT(env.balance(bob) == XRP(1000) + XRP(1));
    }

    void
    testWithFeats(FeatureBitset features)
    {
        // testBadPubKey(features);
        // testUnfunded(features);
        // testSuccess(features);
        // testAtomicFailure(features);
        testFirstFailure(features);

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
