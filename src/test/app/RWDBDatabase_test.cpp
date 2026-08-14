/**
 * @file
 * @brief Tests for RWDBDatabase - in-memory RelationalDatabase implementation.
 */

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/delegate.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>

#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/rdb/backend/RWDBDatabase.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/RangeSet.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/config/Constants.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TxSearched.h>
#include <xrpl/rdb/RelationalDatabase.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace xrpl::test {

class RWDBDatabase_test : public beast::unit_test::Suite
{
    static std::unique_ptr<Config>
    enableRWDB(std::unique_ptr<Config> cfg)
    {
        cfg->section(xrpl::Sections::kRelationalDb).set("backend", "rwdb");
        return cfg;
    }

    static test::jtx::Env
    makeEnv(beast::unit_test::Suite& parent)
    {
        using namespace test::jtx;
        return Env(parent, envconfig([](std::unique_ptr<Config> cfg) {
                       cfg = enableRWDB(std::move(cfg));
                       cfg->fees.referenceFee = 10;
                       return cfg;
                   }));
    }

    static uint256
    missingHash()
    {
        uint256 hash;
        (void)hash.parseHex("1111111111111111111111111111111111111111111111111111111111111111");
        return hash;
    }

    static std::vector<uint256>
    txnSeqOrder(std::shared_ptr<ReadView const> const& ledger)
    {
        std::vector<std::pair<std::uint32_t, uint256>> indexed;
        for (auto const& item : ledger->txs)
        {
            indexed.emplace_back(
                item.second->getFieldU32(sfTransactionIndex), item.first->getTransactionID());
        }
        std::sort(indexed.begin(), indexed.end());
        std::vector<uint256> ids;
        ids.reserve(indexed.size());
        for (auto const& [_, id] : indexed)
            ids.push_back(id);
        return ids;
    }

    static uint256
    txId(RelationalDatabase::AccountTx const& accountTx)
    {
        return accountTx.first ? accountTx.first->getID() : uint256{};
    }

    void
    testDesignVerification()
    {
        testcase("design verification");

        static_assert(
            std::is_base_of_v<RelationalDatabase, RWDBDatabase>,
            "RWDBDatabase must implement RelationalDatabase");

        pass();
    }

    void
    testGetTxHistoryOrdersByTxnSeq()
    {
        testcase("getTxHistory orders a ledger by txnSeq");

        using namespace test::jtx;
        Env env = makeEnv(*this);

        Account const a1{"A1"};
        Account const a2{"A2"};
        Account const a3{"A3"};
        Account const a4{"A4"};
        env.fund(XRP(10000), a1, a2, a3, a4);
        env.close();

        env(noop(a1));
        env(pay(a1, a2, XRP(1)));
        env(noop(a2));
        env(pay(a2, a3, XRP(1)));
        env(noop(a3));
        env(pay(a3, a4, XRP(1)));
        env.close();

        auto const expected = txnSeqOrder(env.closed());
        BEAST_EXPECT(expected.size() >= 6);
        if (expected.size() < 2)
            return;

        auto& db = env.app().getRelationalDatabase();
        auto const history = db.getTxHistory(0);
        BEAST_EXPECT(history.size() >= expected.size());
        if (history.size() < expected.size())
            return;

        for (std::size_t i = 0; i < expected.size(); ++i)
            BEAST_EXPECT(history[i]->getID() == expected[i]);

        auto hashOrder = expected;
        std::sort(hashOrder.begin(), hashOrder.end());
        if (hashOrder != expected)
        {
            std::vector<uint256> actual(expected.size());
            for (std::size_t i = 0; i < expected.size(); ++i)
                actual[i] = history[i]->getID();
            BEAST_EXPECT(actual != hashOrder);
        }

        auto const skipped = db.getTxHistory(2);
        BEAST_EXPECT(skipped.size() + 2 >= expected.size());
        if (skipped.empty())
            return;
        BEAST_EXPECT(skipped.front()->getID() == expected[2]);
    }

    void
    testMarkerResumeAcrossPrunedLedgers()
    {
        testcase("account_tx marker resumes across pruned ledgers");

        using namespace test::jtx;
        Env env = makeEnv(*this);

        Account const a1{"A1"};
        env.fund(XRP(10000), a1);
        env.close();

        for (int i = 0; i < 5; ++i)
        {
            env(noop(a1));
            env.close();
        }

        auto& db = env.app().getRelationalDatabase();
        RelationalDatabase::AccountTxPageOptions opts{
            .account = a1.id(),
            .ledgerRange = {.min = 0, .max = 0},
            .marker = std::nullopt,
            .limit = 1,
            .bAdmin = true,
            .delegate = std::nullopt};

        auto const oldest = db.oldestAccountTxPage(opts);
        BEAST_EXPECT(oldest.second);
        if (!oldest.second)
            return;

        auto const forwardMarker = *oldest.second;
        db.deleteTransactionByLedgerSeq(forwardMarker.ledgerSeq);

        opts.marker = forwardMarker;
        opts.limit = 20;
        auto const forwardResume = db.oldestAccountTxPage(opts);
        BEAST_EXPECT(!forwardResume.first.empty());
        for (auto const& accountTx : forwardResume.first)
        {
            BEAST_EXPECT(accountTx.first);
            if (accountTx.first)
                BEAST_EXPECT(accountTx.first->getLedger() > forwardMarker.ledgerSeq);
        }

        opts.marker = std::nullopt;
        opts.limit = 1;
        auto const newest = db.newestAccountTxPage(opts);
        BEAST_EXPECT(newest.second);
        if (!newest.second)
            return;

        auto const reverseMarker = *newest.second;
        db.deleteTransactionByLedgerSeq(reverseMarker.ledgerSeq);

        opts.marker = reverseMarker;
        opts.limit = 20;
        auto const reverseResume = db.newestAccountTxPage(opts);
        BEAST_EXPECT(!reverseResume.first.empty());
        for (auto const& accountTx : reverseResume.first)
        {
            BEAST_EXPECT(accountTx.first);
            if (accountTx.first)
                BEAST_EXPECT(accountTx.first->getLedger() < reverseMarker.ledgerSeq);
        }
    }

    void
    testDelegateFilterPaging()
    {
        testcase("delegate filter paging skips non-matching rows");

        using namespace test::jtx;
        Env env = makeEnv(*this);

        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(10000), alice, bob, carol);
        env.close();
        env(delegate::set(alice, bob, {"Payment"}));
        env.close();

        env(pay(alice, carol, XRP(1)));
        env.close();
        env(pay(alice, carol, XRP(2)), delegate::As(bob));
        env.close();
        env(noop(alice));
        env.close();
        env(pay(alice, carol, XRP(3)), delegate::As(bob));
        env.close();

        DelegateFilter const actor{.type = DelegateType::Actor, .counterparty = std::nullopt};
        RelationalDatabase::AccountTxPageOptions opts{
            .account = alice.id(),
            .ledgerRange = {.min = 0, .max = 0},
            .marker = std::nullopt,
            .limit = 1,
            .bAdmin = true,
            .delegate = actor};

        auto& db = env.app().getRelationalDatabase();
        std::vector<uint256> paged;
        for (int i = 0; i < 8; ++i)
        {
            auto const page = db.oldestAccountTxPage(opts);
            for (auto const& accountTx : page.first)
                paged.push_back(txId(accountTx));
            if (!page.second)
                break;
            opts.marker = page.second;
        }

        BEAST_EXPECT(paged.size() == 2);
        BEAST_EXPECT(paged[0] != paged[1]);

        opts.marker = std::nullopt;
        opts.limit = 20;
        auto const all = db.oldestAccountTxPage(opts);
        BEAST_EXPECT(all.first.size() == 2);
        if (all.first.size() == 2 && paged.size() == 2)
        {
            BEAST_EXPECT(txId(all.first[0]) == paged[0]);
            BEAST_EXPECT(txId(all.first[1]) == paged[1]);
        }
    }

    void
    testGetTransactionRangeAndInverted()
    {
        testcase("getTransaction range, purge, and inverted interval");

        using namespace test::jtx;
        Env env = makeEnv(*this);

        Account const a1{"A1"};
        env.fund(XRP(10000), a1);
        env.close();
        env(noop(a1));
        auto const knownHash = env.tx()->getTransactionID();
        env.close();
        auto const purgedSeq = env.closed()->header().seq;
        env.close();

        auto& db = env.app().getRelationalDatabase();
        auto const minSeq = db.getMinLedgerSeq();
        auto const maxSeq = db.getMaxLedgerSeq();
        BEAST_EXPECT(minSeq && maxSeq);
        if (!minSeq || !maxSeq)
            return;

        ErrorCodeI ec = RpcSuccess;
        auto const found = db.getTransaction(knownHash, std::nullopt, ec);
        BEAST_EXPECT(std::holds_alternative<RelationalDatabase::AccountTx>(found));

        auto const missing = missingHash();
        auto const unknown = db.getTransaction(missing, std::nullopt, ec);
        BEAST_EXPECT(std::holds_alternative<TxSearched>(unknown));
        if (auto const* searched = std::get_if<TxSearched>(&unknown))
            BEAST_EXPECT(*searched == TxSearched::Unknown);

        auto const allPresent =
            db.getTransaction(missing, ClosedInterval<std::uint32_t>(*minSeq, *maxSeq), ec);
        BEAST_EXPECT(std::holds_alternative<TxSearched>(allPresent));
        if (auto const* searched = std::get_if<TxSearched>(&allPresent))
            BEAST_EXPECT(*searched == TxSearched::All);

        if (*maxSeq > *minSeq)
        {
            auto const inverted =
                db.getTransaction(missing, ClosedInterval<std::uint32_t>(*maxSeq, *minSeq), ec);
            BEAST_EXPECT(std::holds_alternative<TxSearched>(inverted));
            if (auto const* searched = std::get_if<TxSearched>(&inverted))
                BEAST_EXPECT(*searched == TxSearched::Some);

            auto const foundInverted =
                db.getTransaction(knownHash, ClosedInterval<std::uint32_t>(*maxSeq, *minSeq), ec);
            BEAST_EXPECT(std::holds_alternative<RelationalDatabase::AccountTx>(foundInverted));
        }

        db.deleteTransactionByLedgerSeq(purgedSeq);
        auto const afterPurge =
            db.getTransaction(missing, ClosedInterval<std::uint32_t>(*minSeq, *maxSeq), ec);
        BEAST_EXPECT(std::holds_alternative<TxSearched>(afterPurge));
        if (auto const* searched = std::get_if<TxSearched>(&afterPurge))
            BEAST_EXPECT(*searched == TxSearched::Some);
    }

    void
    testIdempotentDoubleSave()
    {
        testcase("saveValidatedLedger is idempotent");

        using namespace test::jtx;
        Env env = makeEnv(*this);

        Account const a1{"A1"};
        env.fund(XRP(10000), a1);
        env.close();
        env(noop(a1));
        env.close();

        auto const closed = std::dynamic_pointer_cast<Ledger const>(env.closed());
        BEAST_EXPECT(closed);
        if (!closed)
            return;

        auto& db = env.app().getRelationalDatabase();
        auto const txCount = db.getTransactionCount();
        auto const acctCount = db.getAccountTransactionCount();
        BEAST_EXPECT(txCount > 0);
        BEAST_EXPECT(acctCount > 0);

        BEAST_EXPECT(db.saveValidatedLedger(closed, false));
        BEAST_EXPECT(db.saveValidatedLedger(closed, false));
        BEAST_EXPECT(db.getTransactionCount() == txCount);
        BEAST_EXPECT(db.getAccountTransactionCount() == acctCount);

        auto const history = db.getTxHistory(0);
        std::vector<uint256> ids;
        ids.reserve(history.size());
        for (auto const& tx : history)
            ids.push_back(tx->getID());
        auto unique = ids;
        std::sort(unique.begin(), unique.end());
        unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
        BEAST_EXPECT(unique.size() == ids.size());
    }

public:
    void
    run() override
    {
        testDesignVerification();
        testGetTxHistoryOrdersByTxnSeq();
        testMarkerResumeAcrossPrunedLedgers();
        testDelegateFilterPaging();
        testGetTransactionRangeAndInverted();
        testIdempotentDoubleSave();
    }
};

BEAST_DEFINE_TESTSUITE(RWDBDatabase, app, xrpl);

}  // namespace xrpl::test
