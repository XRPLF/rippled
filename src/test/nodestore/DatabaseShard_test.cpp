//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/misc/SHAMapStore.h>
#include <ripple/app/rdb/backend/RelationalDBInterfaceSqlite.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/random.h>
#include <ripple/beast/hash/hash_append.h>
#include <ripple/beast/utility/temp_dir.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/nodestore/DummyScheduler.h>
#include <ripple/nodestore/impl/DecodedBlob.h>
#include <ripple/nodestore/impl/Shard.h>
#include <ripple/protocol/digest.h>
#include <test/jtx.h>
#include <test/jtx/CaptureLogs.h>
#include <test/nodestore/TestBase.h>

#include <boost/algorithm/hex.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>
#include <openssl/ripemd.h>

namespace ripple {
namespace NodeStore {

/** std::uniform_int_distribution is platform dependent.
 *  Unit test for deterministic shards is the following: it generates
 *  predictable accounts and transactions, packs them into ledgers
 *  and makes the shard. The hash of this shard should be equal to the
 *  given value. On different platforms (precisely, Linux and Mac)
 *  hashes of the resulting shard was different. It was unvestigated
 *  that the problem is in the class std::uniform_int_distribution
 *  which generates different pseudorandom sequences on different
 *  platforms, but we need predictable sequence.
 */
template <class IntType = int>
struct uniformIntDistribution
{
    using resultType = IntType;

    const resultType A, B;

    struct paramType
    {
        const resultType A, B;

        paramType(resultType aa, resultType bb) : A(aa), B(bb)
        {
        }
    };

    explicit uniformIntDistribution(
        const resultType a = 0,
        const resultType b = std::numeric_limits<resultType>::max())
        : A(a), B(b)
    {
    }

    explicit uniformIntDistribution(const paramType& params)
        : A(params.A), B(params.B)
    {
    }

    template <class Generator>
    resultType
    operator()(Generator& g) const
    {
        return rnd(g, A, B);
    }

    template <class Generator>
    resultType
    operator()(Generator& g, const paramType& params) const
    {
        return rnd(g, params.A, params.B);
    }

    resultType
    a() const
    {
        return A;
    }

    resultType
    b() const
    {
        return B;
    }

    resultType
    min() const
    {
        return A;
    }

    resultType
    max() const
    {
        return B;
    }

private:
    template <class Generator>
    resultType
    rnd(Generator& g, const resultType a, const resultType b) const
    {
        static_assert(
            std::is_convertible<typename Generator::result_type, resultType>::
                value,
            "Ups...");
        static_assert(
            Generator::min() == 0, "If non-zero we have handle the offset");
        const resultType range = b - a + 1;
        assert(Generator::max() >= range);  // Just for safety
        const resultType rejectLim = g.max() % range;
        resultType n;
        do
            n = g();
        while (n <= rejectLim);
        return (n % range) + a;
    }
};

template <class Engine, class Integral>
Integral
randInt(Engine& engine, Integral min, Integral max)
{
    assert(max > min);

    // This should have no state and constructing it should
    // be very cheap. If that turns out not to be the case
    // it could be hand-optimized.
    return uniformIntDistribution<Integral>(min, max)(engine);
}

template <class Engine, class Integral>
Integral
randInt(Engine& engine, Integral max)
{
    return randInt(engine, Integral(0), max);
}

// Tests DatabaseShard class
//
class DatabaseShard_test : public TestBase
{
    static constexpr std::uint32_t maxSizeGb = 10;
    static constexpr std::uint32_t maxHistoricalShards = 100;
    static constexpr std::uint32_t ledgersPerShard = 256;
    static constexpr std::uint32_t earliestSeq = ledgersPerShard + 1;
    static constexpr std::uint32_t dataSizeMax = 4;
    static constexpr std::uint32_t iniAmount = 1000000;
    static constexpr std::uint32_t nTestShards = 4;
    static constexpr std::chrono::seconds shardStoreTimeout =
        std::chrono::seconds(60);
    test::SuiteJournal journal_;
    beast::temp_dir defNodeDir;

    struct TestData
    {
        /* ring used to generate pseudo-random sequence */
        beast::xor_shift_engine rng_;
        /* number of shards to generate */
        int numShards_;
        /* vector of accounts used to send test transactions */
        std::vector<test::jtx::Account> accounts_;
        /* nAccounts_[i] is the number of these accounts existed before i-th
         * ledger */
        std::vector<int> nAccounts_;
        /* payAccounts_[i][j] = {from, to} is the pair which consists of two
         * number of accounts: source and destinations, which participate in
         * j-th payment on i-th ledger */
        std::vector<std::vector<std::pair<int, int>>> payAccounts_;
        /* xrpAmount_[i] is the amount for all payments on i-th ledger */
        std::vector<int> xrpAmount_;
        /* ledgers_[i] is the i-th ledger which contains the above described
         * accounts and payments */
        std::vector<std::shared_ptr<const Ledger>> ledgers_;

        TestData(
            std::uint64_t const seedValue,
            int dataSize = dataSizeMax,
            int numShards = 1)
            : rng_(seedValue), numShards_(numShards)
        {
            std::uint32_t n = 0;
            std::uint32_t nLedgers = ledgersPerShard * numShards;

            nAccounts_.reserve(nLedgers);
            payAccounts_.reserve(nLedgers);
            xrpAmount_.reserve(nLedgers);

            for (std::uint32_t i = 0; i < nLedgers; ++i)
            {
                int p;
                if (n >= 2)
                    p = randInt(rng_, 2 * dataSize);
                else
                    p = 0;

                std::vector<std::pair<int, int>> pay;
                pay.reserve(p);

                for (int j = 0; j < p; ++j)
                {
                    int from, to;
                    do
                    {
                        from = randInt(rng_, n - 1);
                        to = randInt(rng_, n - 1);
                    } while (from == to);

                    pay.push_back(std::make_pair(from, to));
                }

                n += !randInt(rng_, nLedgers / dataSize);

                if (n > accounts_.size())
                {
                    char str[9];
                    for (int j = 0; j < 8; ++j)
                        str[j] = 'a' + randInt(rng_, 'z' - 'a');
                    str[8] = 0;
                    accounts_.emplace_back(str);
                }

                nAccounts_.push_back(n);
                payAccounts_.push_back(std::move(pay));
                xrpAmount_.push_back(randInt(rng_, 90) + 10);
            }
        }

        bool
        isNewAccounts(int seq)
        {
            return nAccounts_[seq] > (seq ? nAccounts_[seq - 1] : 0);
        }

        void
        makeLedgerData(test::jtx::Env& env_, std::uint32_t seq)
        {
            using namespace test::jtx;

            if (isNewAccounts(seq))
                env_.fund(XRP(iniAmount), accounts_[nAccounts_[seq] - 1]);

            for (std::uint32_t i = 0; i < payAccounts_[seq].size(); ++i)
            {
                env_(
                    pay(accounts_[payAccounts_[seq][i].first],
                        accounts_[payAccounts_[seq][i].second],
                        XRP(xrpAmount_[seq])));
            }
        }

        bool
        makeLedgers(test::jtx::Env& env_, std::uint32_t startIndex = 0)
        {
            if (startIndex == 0)
            {
                for (std::uint32_t i = 3; i <= ledgersPerShard; ++i)
                {
                    if (!env_.close())
                        return false;
                    std::shared_ptr<const Ledger> ledger =
                        env_.app().getLedgerMaster().getClosedLedger();
                    if (ledger->info().seq != i)
                        return false;
                }
            }

            for (std::uint32_t i = 0; i < ledgersPerShard * numShards_; ++i)
            {
                auto const index = i + (startIndex * ledgersPerShard);

                makeLedgerData(env_, i);
                if (!env_.close())
                    return false;
                std::shared_ptr<const Ledger> ledger =
                    env_.app().getLedgerMaster().getClosedLedger();
                if (ledger->info().seq != index + ledgersPerShard + 1)
                    return false;
                ledgers_.push_back(ledger);
            }

            return true;
        }
    };

    void
    testLedgerData(
        TestData& data,
        std::shared_ptr<Ledger> ledger,
        std::uint32_t seq)
    {
        using namespace test::jtx;

        auto rootCount{0};
        auto accCount{0};
        auto sothCount{0};
        for (auto const& sles : ledger->sles)
        {
            if (sles->getType() == ltACCOUNT_ROOT)
            {
                int sq = sles->getFieldU32(sfSequence);
                int reqsq = -1;
                const auto id = sles->getAccountID(sfAccount);

                for (int i = 0; i < data.accounts_.size(); ++i)
                {
                    if (id == data.accounts_[i].id())
                    {
                        reqsq = ledgersPerShard + 1;
                        for (int j = 0; j <= seq; ++j)
                            if (data.nAccounts_[j] > i + 1 ||
                                (data.nAccounts_[j] == i + 1 &&
                                 !data.isNewAccounts(j)))
                            {
                                for (int k = 0; k < data.payAccounts_[j].size();
                                     ++k)
                                    if (data.payAccounts_[j][k].first == i)
                                        reqsq++;
                            }
                            else
                                reqsq++;
                        ++accCount;
                        break;
                    }
                }
                if (reqsq == -1)
                {
                    reqsq = data.nAccounts_[seq] + 1;
                    ++rootCount;
                }
                BEAST_EXPECT(sq == reqsq);
            }
            else
                ++sothCount;
        }
        BEAST_EXPECT(rootCount == 1);
        BEAST_EXPECT(accCount == data.nAccounts_[seq]);
        BEAST_EXPECT(sothCount == 3);

        auto iniCount{0};
        auto setCount{0};
        auto payCount{0};
        auto tothCount{0};
        for (auto const& tx : ledger->txs)
        {
            if (tx.first->getTxnType() == ttPAYMENT)
            {
                std::int64_t xrpAmount =
                    tx.first->getFieldAmount(sfAmount).xrp().decimalXRP();
                if (xrpAmount == iniAmount)
                    ++iniCount;
                else
                {
                    ++payCount;
                    BEAST_EXPECT(xrpAmount == data.xrpAmount_[seq]);
                }
            }
            else if (tx.first->getTxnType() == ttACCOUNT_SET)
                ++setCount;
            else
                ++tothCount;
        }
        int newacc = data.isNewAccounts(seq) ? 1 : 0;
        BEAST_EXPECT(iniCount == newacc);
        BEAST_EXPECT(setCount == newacc);
        BEAST_EXPECT(payCount == data.payAccounts_[seq].size());
        BEAST_EXPECT(tothCount == !seq);
    }

    bool
    saveLedger(
        Database& db,
        Ledger const& ledger,
        std::shared_ptr<Ledger const> const& next = {})
    {
        // Store header
        {
            Serializer s(sizeof(std::uint32_t) + sizeof(LedgerInfo));
            s.add32(HashPrefix::ledgerMaster);
            addRaw(ledger.info(), s);
            db.store(
                hotLEDGER,
                std::move(s.modData()),
                ledger.info().hash,
                ledger.info().seq);
        }

        // Store the state map
        auto visitAcc = [&](SHAMapTreeNode const& node) {
            Serializer s;
            node.serializeWithPrefix(s);
            db.store(
                node.getType() == SHAMapNodeType::tnINNER ? hotUNKNOWN
                                                          : hotACCOUNT_NODE,
                std::move(s.modData()),
                node.getHash().as_uint256(),
                ledger.info().seq);
            return true;
        };

        if (ledger.stateMap().getHash().isNonZero())
        {
            if (!ledger.stateMap().isValid())
                return false;
            if (next && next->info().parentHash == ledger.info().hash)
            {
                auto have = next->stateMap().snapShot(false);
                ledger.stateMap().snapShot(false)->visitDifferences(
                    &(*have), visitAcc);
            }
            else
                ledger.stateMap().snapShot(false)->visitNodes(visitAcc);
        }

        // Store the transaction map
        auto visitTx = [&](SHAMapTreeNode& node) {
            Serializer s;
            node.serializeWithPrefix(s);
            db.store(
                node.getType() == SHAMapNodeType::tnINNER ? hotUNKNOWN
                                                          : hotTRANSACTION_NODE,
                std::move(s.modData()),
                node.getHash().as_uint256(),
                ledger.info().seq);
            return true;
        };

        if (ledger.info().txHash.isNonZero())
        {
            if (!ledger.txMap().isValid())
                return false;
            ledger.txMap().snapShot(false)->visitNodes(visitTx);
        }

        return true;
    }

    void
    checkLedger(TestData& data, DatabaseShard& db, Ledger const& ledger)
    {
        auto fetched = db.fetchLedger(ledger.info().hash, ledger.info().seq);
        if (!BEAST_EXPECT(fetched))
            return;

        testLedgerData(data, fetched, ledger.info().seq - ledgersPerShard - 1);

        // verify the metadata/header info by serializing to json
        BEAST_EXPECT(
            getJson(LedgerFill{
                ledger, nullptr, LedgerFill::full | LedgerFill::expand}) ==
            getJson(LedgerFill{
                *fetched, nullptr, LedgerFill::full | LedgerFill::expand}));

        BEAST_EXPECT(
            getJson(LedgerFill{
                ledger, nullptr, LedgerFill::full | LedgerFill::binary}) ==
            getJson(LedgerFill{
                *fetched, nullptr, LedgerFill::full | LedgerFill::binary}));

        // walk shamap and validate each node
        auto fcompAcc = [&](SHAMapTreeNode& node) -> bool {
            Serializer s;
            node.serializeWithPrefix(s);
            auto nSrc{NodeObject::createObject(
                node.getType() == SHAMapNodeType::tnINNER ? hotUNKNOWN
                                                          : hotACCOUNT_NODE,
                std::move(s.modData()),
                node.getHash().as_uint256())};
            if (!BEAST_EXPECT(nSrc))
                return false;

            auto nDst = db.fetchNodeObject(
                node.getHash().as_uint256(), ledger.info().seq);
            if (!BEAST_EXPECT(nDst))
                return false;

            BEAST_EXPECT(isSame(nSrc, nDst));

            return true;
        };
        if (ledger.stateMap().getHash().isNonZero())
            ledger.stateMap().snapShot(false)->visitNodes(fcompAcc);

        auto fcompTx = [&](SHAMapTreeNode& node) -> bool {
            Serializer s;
            node.serializeWithPrefix(s);
            auto nSrc{NodeObject::createObject(
                node.getType() == SHAMapNodeType::tnINNER ? hotUNKNOWN
                                                          : hotTRANSACTION_NODE,
                std::move(s.modData()),
                node.getHash().as_uint256())};
            if (!BEAST_EXPECT(nSrc))
                return false;

            auto nDst = db.fetchNodeObject(
                node.getHash().as_uint256(), ledger.info().seq);
            if (!BEAST_EXPECT(nDst))
                return false;

            BEAST_EXPECT(isSame(nSrc, nDst));

            return true;
        };
        if (ledger.info().txHash.isNonZero())
            ledger.txMap().snapShot(false)->visitNodes(fcompTx);
    }

    std::string
    bitmask2Rangeset(std::uint64_t bitmask)
    {
        std::string set;
        if (!bitmask)
            return set;
        bool empty = true;

        for (std::uint32_t i = 0; i < 64 && bitmask; i++)
        {
            if (bitmask & (1ll << i))
            {
                if (!empty)
                    set += ",";
                set += std::to_string(i);
                empty = false;
            }
        }

        RangeSet<std::uint32_t> rs;
        BEAST_EXPECT(from_string(rs, set));
        return ripple::to_string(rs);
    }

    std::unique_ptr<Config>
    testConfig(
        std::string const& shardDir,
        std::string const& nodeDir = std::string())
    {
        using namespace test::jtx;

        return envconfig([&](std::unique_ptr<Config> cfg) {
            // Shard store configuration
            cfg->overwrite(ConfigSection::shardDatabase(), "path", shardDir);
            cfg->overwrite(
                ConfigSection::shardDatabase(),
                "max_historical_shards",
                std::to_string(maxHistoricalShards));
            cfg->overwrite(
                ConfigSection::shardDatabase(),
                "ledgers_per_shard",
                std::to_string(ledgersPerShard));
            cfg->overwrite(
                ConfigSection::shardDatabase(),
                "earliest_seq",
                std::to_string(earliestSeq));

            // Node store configuration
            cfg->overwrite(
                ConfigSection::nodeDatabase(),
                "path",
                nodeDir.empty() ? defNodeDir.path() : nodeDir);
            cfg->overwrite(
                ConfigSection::nodeDatabase(),
                "ledgers_per_shard",
                std::to_string(ledgersPerShard));
            cfg->overwrite(
                ConfigSection::nodeDatabase(),
                "earliest_seq",
                std::to_string(earliestSeq));
            return cfg;
        });
    }

    std::optional<std::uint32_t>
    waitShard(
        DatabaseShard& shardStore,
        std::uint32_t shardIndex,
        std::chrono::seconds timeout = shardStoreTimeout)
    {
        auto const end{std::chrono::system_clock::now() + timeout};
        while (shardStore.getNumTasks() ||
               !boost::icl::contains(
                   shardStore.getShardInfo()->finalized(), shardIndex))
        {
            if (!BEAST_EXPECT(std::chrono::system_clock::now() < end))
                return std::nullopt;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        return shardIndex;
    }

    std::optional<std::uint32_t>
    createShard(
        TestData& data,
        DatabaseShard& shardStore,
        int maxShardIndex = 1,
        int shardOffset = 0)
    {
        int shardIndex{-1};

        for (std::uint32_t i = 0; i < ledgersPerShard; ++i)
        {
            auto const ledgerSeq{shardStore.prepareLedger(
                (maxShardIndex + 1) * ledgersPerShard)};
            if (!BEAST_EXPECT(ledgerSeq != std::nullopt))
                return std::nullopt;

            shardIndex = shardStore.seqToShardIndex(*ledgerSeq);

            int const arrInd = *ledgerSeq - (ledgersPerShard * shardOffset) -
                ledgersPerShard - 1;
            BEAST_EXPECT(
                arrInd >= 0 && arrInd < maxShardIndex * ledgersPerShard);
            BEAST_EXPECT(saveLedger(shardStore, *data.ledgers_[arrInd]));
            if (arrInd % ledgersPerShard == (ledgersPerShard - 1))
            {
                uint256 const finalKey_{0};
                Serializer s;
                s.add32(Shard::version);
                s.add32(shardStore.firstLedgerSeq(shardIndex));
                s.add32(shardStore.lastLedgerSeq(shardIndex));
                s.addRaw(data.ledgers_[arrInd]->info().hash.data(), 256 / 8);
                shardStore.store(
                    hotUNKNOWN, std::move(s.modData()), finalKey_, *ledgerSeq);
            }
            shardStore.setStored(data.ledgers_[arrInd]);
        }

        return waitShard(shardStore, shardIndex);
    }

    void
    testStandalone()
    {
        testcase("Standalone");

        using namespace test::jtx;

        beast::temp_dir shardDir;
        DummyScheduler scheduler;
        {
            Env env{*this, testConfig(shardDir.path())};
            std::unique_ptr<DatabaseShard> shardStore{
                make_ShardStore(env.app(), scheduler, 2, journal_)};

            BEAST_EXPECT(shardStore);
            BEAST_EXPECT(shardStore->init());
            BEAST_EXPECT(shardStore->ledgersPerShard() == ledgersPerShard);
            BEAST_EXPECT(shardStore->seqToShardIndex(ledgersPerShard + 1) == 1);
            BEAST_EXPECT(shardStore->seqToShardIndex(2 * ledgersPerShard) == 1);
            BEAST_EXPECT(
                shardStore->seqToShardIndex(2 * ledgersPerShard + 1) == 2);
            BEAST_EXPECT(
                shardStore->earliestShardIndex() ==
                (earliestSeq - 1) / ledgersPerShard);
            BEAST_EXPECT(shardStore->firstLedgerSeq(1) == ledgersPerShard + 1);
            BEAST_EXPECT(shardStore->lastLedgerSeq(1) == 2 * ledgersPerShard);
            BEAST_EXPECT(shardStore->getRootDir().string() == shardDir.path());
        }

        {
            Env env{*this, testConfig(shardDir.path())};
            std::unique_ptr<DatabaseShard> shardStore{
                make_ShardStore(env.app(), scheduler, 2, journal_)};

            env.app().config().overwrite(
                ConfigSection::shardDatabase(), "ledgers_per_shard", "512");
            BEAST_EXPECT(!shardStore->init());
        }

        Env env{*this, testConfig(shardDir.path())};
        std::unique_ptr<DatabaseShard> shardStore{
            make_ShardStore(env.app(), scheduler, 2, journal_)};

        env.app().config().overwrite(
            ConfigSection::shardDatabase(),
            "earliest_seq",
            std::to_string(std::numeric_limits<std::uint32_t>::max()));
        BEAST_EXPECT(!shardStore->init());
    }

    void
    testCreateShard(std::uint64_t const seedValue)
    {
        testcase("Create shard");

        using namespace test::jtx;

        beast::temp_dir shardDir;
        Env env{*this, testConfig(shardDir.path())};
        DatabaseShard* db = env.app().getShardStore();
        BEAST_EXPECT(db);

        TestData data(seedValue);
        if (!BEAST_EXPECT(data.makeLedgers(env)))
            return;

        if (!createShard(data, *db, 1))
            return;

        for (std::uint32_t i = 0; i < ledgersPerShard; ++i)
            checkLedger(data, *db, *data.ledgers_[i]);
    }

    void
    testReopenDatabase(std::uint64_t const seedValue)
    {
        testcase("Reopen shard store");

        using namespace test::jtx;

        beast::temp_dir shardDir;
        {
            Env env{*this, testConfig(shardDir.path())};
            DatabaseShard* db = env.app().getShardStore();
            BEAST_EXPECT(db);

            TestData data(seedValue, 4, 2);
            if (!BEAST_EXPECT(data.makeLedgers(env)))
                return;

            for (auto i = 0; i < 2; ++i)
            {
                if (!createShard(data, *db, 2))
                    return;
            }
        }
        {
            Env env{*this, testConfig(shardDir.path())};
            DatabaseShard* db = env.app().getShardStore();
            BEAST_EXPECT(db);

            TestData data(seedValue, 4, 2);
            if (!BEAST_EXPECT(data.makeLedgers(env)))
                return;

            for (std::uint32_t i = 1; i <= 2; ++i)
                waitShard(*db, i);

            for (std::uint32_t i = 0; i < 2 * ledgersPerShard; ++i)
                checkLedger(data, *db, *data.ledgers_[i]);
        }
    }

    void
    testGetFinalShards(std::uint64_t const seedValue)
    {
        testcase("Get final shards");

        using namespace test::jtx;

        beast::temp_dir shardDir;
        Env env{*this, testConfig(shardDir.path())};
        DatabaseShard* db = env.app().getShardStore();
        BEAST_EXPECT(db);

        TestData data(seedValue, 2, nTestShards);
        if (!BEAST_EXPECT(data.makeLedgers(env)))
            return;

        BEAST_EXPECT(db->getShardInfo()->finalized().empty());

        for (auto i = 0; i < nTestShards; ++i)
        {
            auto const shardIndex{createShard(data, *db, nTestShards)};
            if (!BEAST_EXPECT(
                    shardIndex && *shardIndex >= 1 &&
                    *shardIndex <= nTestShards))
            {
                return;
            }

            BEAST_EXPECT(boost::icl::contains(
                db->getShardInfo()->finalized(), *shardIndex));
        }
    }

    void
    testPrepareShards(std::uint64_t const seedValue)
    {
        testcase("Prepare shards");

        using namespace test::jtx;

        beast::temp_dir shardDir;
        Env env{*this, testConfig(shardDir.path())};
        DatabaseShard* db = env.app().getShardStore();
        BEAST_EXPECT(db);

        TestData data(seedValue, 1, nTestShards);
        if (!BEAST_EXPECT(data.makeLedgers(env)))
            return;

        BEAST_EXPECT(db->getPreShards() == "");
        BEAST_EXPECT(!db->prepareShards({}));

        std::uint64_t bitMask = 0;
        for (std::uint32_t i = 0; i < nTestShards * 2; ++i)
        {
            std::uint32_t const shardIndex{
                randInt(data.rng_, nTestShards - 1) + 1};
            if (bitMask & (1ll << shardIndex))
            {
                db->removePreShard(shardIndex);
                bitMask &= ~(1ll << shardIndex);
            }
            else
            {
                BEAST_EXPECT(db->prepareShards({shardIndex}));
                bitMask |= 1ll << shardIndex;
            }
            BEAST_EXPECT(db->getPreShards() == bitmask2Rangeset(bitMask));
        }

        // test illegal cases
        // adding shards with too large number
        BEAST_EXPECT(!db->prepareShards({0}));
        BEAST_EXPECT(db->getPreShards() == bitmask2Rangeset(bitMask));
        BEAST_EXPECT(!db->prepareShards({nTestShards + 1}));
        BEAST_EXPECT(db->getPreShards() == bitmask2Rangeset(bitMask));
        BEAST_EXPECT(!db->prepareShards({nTestShards + 2}));
        BEAST_EXPECT(db->getPreShards() == bitmask2Rangeset(bitMask));

        // create shards which are not prepared for import
        BEAST_EXPECT(db->getShardInfo()->finalized().empty());

        std::uint64_t bitMask2 = 0;
        for (auto i = 0; i < nTestShards; ++i)
        {
            auto const shardIndex{createShard(data, *db, nTestShards)};
            if (!BEAST_EXPECT(
                    shardIndex && *shardIndex >= 1 &&
                    *shardIndex <= nTestShards))
            {
                return;
            }

            BEAST_EXPECT(boost::icl::contains(
                db->getShardInfo()->finalized(), *shardIndex));

            bitMask2 |= 1ll << *shardIndex;
            BEAST_EXPECT((bitMask & bitMask2) == 0);
            if ((bitMask | bitMask2) == ((1ll << nTestShards) - 1) << 1)
                break;
        }

        // try to create another shard
        BEAST_EXPECT(
            db->prepareLedger((nTestShards + 1) * ledgersPerShard) ==
            std::nullopt);
    }

    void
    testImportShard(std::uint64_t const seedValue)
    {
        testcase("Import shard");

        using namespace test::jtx;

        beast::temp_dir importDir;
        TestData data(seedValue, 2);

        {
            Env env{*this, testConfig(importDir.path())};
            DatabaseShard* db = env.app().getShardStore();
            BEAST_EXPECT(db);

            if (!BEAST_EXPECT(data.makeLedgers(env)))
                return;

            if (!createShard(data, *db, 1))
                return;

            for (std::uint32_t i = 0; i < ledgersPerShard; ++i)
                checkLedger(data, *db, *data.ledgers_[i]);

            data.ledgers_.clear();
        }

        boost::filesystem::path importPath(importDir.path());
        importPath /= "1";

        {
            beast::temp_dir shardDir;
            Env env{*this, testConfig(shardDir.path())};
            DatabaseShard* db = env.app().getShardStore();
            BEAST_EXPECT(db);

            if (!BEAST_EXPECT(data.makeLedgers(env)))
                return;

            BEAST_EXPECT(!db->importShard(1, importPath / "not_exist"));
            BEAST_EXPECT(db->prepareShards({1}));
            BEAST_EXPECT(db->getPreShards() == "1");

            using namespace boost::filesystem;
            remove_all(importPath / LgrDBName);
            remove_all(importPath / TxDBName);

            if (!BEAST_EXPECT(db->importShard(1, importPath)))
                return;

            BEAST_EXPECT(db->getPreShards() == "");

            auto n = waitShard(*db, 1);
            if (!BEAST_EXPECT(n && *n == 1))
                return;

            for (std::uint32_t i = 0; i < ledgersPerShard; ++i)
                checkLedger(data, *db, *data.ledgers_[i]);
        }
    }

    void
    testCorruptedDatabase(std::uint64_t const seedValue)
    {
        testcase("Corrupted shard store");

        using namespace test::jtx;

        beast::temp_dir shardDir;
        {
            TestData data(seedValue, 4, 2);
            {
                Env env{*this, testConfig(shardDir.path())};
                DatabaseShard* db = env.app().getShardStore();
                BEAST_EXPECT(db);

                if (!BEAST_EXPECT(data.makeLedgers(env)))
                    return;

                for (auto i = 0; i < 2; ++i)
                {
                    if (!BEAST_EXPECT(createShard(data, *db, 2)))
                        return;
                }
            }

            boost::filesystem::path path = shardDir.path();
            path /= std::string("2");
            path /= "nudb.dat";

            FILE* f = fopen(path.string().c_str(), "r+b");
            if (!BEAST_EXPECT(f))
                return;
            char buf[256];
            beast::rngfill(buf, sizeof(buf), data.rng_);
            BEAST_EXPECT(fwrite(buf, 1, 256, f) == 256);
            fclose(f);
        }

        Env env{*this, testConfig(shardDir.path())};
        DatabaseShard* db = env.app().getShardStore();
        BEAST_EXPECT(db);

        TestData data(seedValue, 4, 2);
        if (!BEAST_EXPECT(data.makeLedgers(env)))
            return;

        for (std::uint32_t shardIndex = 1; shardIndex <= 1; ++shardIndex)
            waitShard(*db, shardIndex);

        BEAST_EXPECT(boost::icl::contains(db->getShardInfo()->finalized(), 1));

        for (std::uint32_t i = 0; i < 1 * ledgersPerShard; ++i)
            checkLedger(data, *db, *data.ledgers_[i]);
    }

    void
    testIllegalFinalKey(std::uint64_t const seedValue)
    {
        testcase("Illegal finalKey");

        using namespace test::jtx;

        for (int i = 0; i < 5; ++i)
        {
            beast::temp_dir shardDir;
            {
                Env env{*this, testConfig(shardDir.path())};
                DatabaseShard* db = env.app().getShardStore();
                BEAST_EXPECT(db);

                TestData data(seedValue + i, 2);
                if (!BEAST_EXPECT(data.makeLedgers(env)))
                    return;

                int shardIndex{-1};
                for (std::uint32_t j = 0; j < ledgersPerShard; ++j)
                {
                    auto const ledgerSeq{
                        db->prepareLedger(2 * ledgersPerShard)};
                    if (!BEAST_EXPECT(ledgerSeq != std::nullopt))
                        return;

                    shardIndex = db->seqToShardIndex(*ledgerSeq);
                    int arrInd = *ledgerSeq - ledgersPerShard - 1;
                    BEAST_EXPECT(arrInd >= 0 && arrInd < ledgersPerShard);
                    BEAST_EXPECT(saveLedger(*db, *data.ledgers_[arrInd]));
                    if (arrInd % ledgersPerShard == (ledgersPerShard - 1))
                    {
                        uint256 const finalKey_{0};
                        Serializer s;
                        s.add32(Shard::version + (i == 0));
                        s.add32(db->firstLedgerSeq(shardIndex) + (i == 1));
                        s.add32(db->lastLedgerSeq(shardIndex) - (i == 3));
                        s.addRaw(
                            data.ledgers_[arrInd - (i == 4)]
                                ->info()
                                .hash.data(),
                            256 / 8);
                        db->store(
                            hotUNKNOWN,
                            std::move(s.modData()),
                            finalKey_,
                            *ledgerSeq);
                    }
                    db->setStored(data.ledgers_[arrInd]);
                }

                if (i == 2)
                {
                    waitShard(*db, shardIndex);
                    BEAST_EXPECT(boost::icl::contains(
                        db->getShardInfo()->finalized(), 1));
                }
                else
                {
                    boost::filesystem::path path(shardDir.path());
                    path /= "1";
                    boost::system::error_code ec;
                    auto start = std::chrono::system_clock::now();
                    auto end = start + shardStoreTimeout;
                    while (std::chrono::system_clock::now() < end &&
                           boost::filesystem::exists(path, ec))
                    {
                        std::this_thread::yield();
                    }

                    BEAST_EXPECT(db->getShardInfo()->finalized().empty());
                }
            }

            {
                Env env{*this, testConfig(shardDir.path())};
                DatabaseShard* db = env.app().getShardStore();
                BEAST_EXPECT(db);

                TestData data(seedValue + i, 2);
                if (!BEAST_EXPECT(data.makeLedgers(env)))
                    return;

                if (i == 2)
                {
                    waitShard(*db, 1);
                    BEAST_EXPECT(boost::icl::contains(
                        db->getShardInfo()->finalized(), 1));

                    for (std::uint32_t j = 0; j < ledgersPerShard; ++j)
                        checkLedger(data, *db, *data.ledgers_[j]);
                }
                else
                    BEAST_EXPECT(db->getShardInfo()->finalized().empty());
            }
        }
    }

    std::string
    ripemd160File(std::string filename)
    {
        using beast::hash_append;
        std::ifstream input(filename, std::ios::in | std::ios::binary);
        char buf[4096];
        ripemd160_hasher h;

        while (input.read(buf, 4096), input.gcount() > 0)
            hash_append(h, buf, input.gcount());

        auto const binResult = static_cast<ripemd160_hasher::result_type>(h);
        const auto charDigest = binResult.data();
        std::string result;
        boost::algorithm::hex(
            charDigest,
            charDigest + sizeof(binResult),
            std::back_inserter(result));

        return result;
    }

    void
    testDeterministicShard(std::uint64_t const seedValue)
    {
        testcase("Deterministic shards");

        using namespace test::jtx;

        for (int i = 0; i < 2; i++)
        {
            beast::temp_dir shardDir;
            {
                Env env{*this, testConfig(shardDir.path())};
                DatabaseShard* db = env.app().getShardStore();
                BEAST_EXPECT(db);

                TestData data(seedValue, 4);
                if (!BEAST_EXPECT(data.makeLedgers(env)))
                    return;

                if (!BEAST_EXPECT(createShard(data, *db) != std::nullopt))
                    return;
            }

            boost::filesystem::path path(shardDir.path());
            path /= "1";

            auto static const ripemd160Key =
                ripemd160File((path / "nudb.key").string());
            auto static const ripemd160Dat =
                ripemd160File((path / "nudb.dat").string());

            {
                Env env{*this, testConfig(shardDir.path())};
                DatabaseShard* db = env.app().getShardStore();
                BEAST_EXPECT(db);

                TestData data(seedValue, 4);
                if (!BEAST_EXPECT(data.makeLedgers(env)))
                    return;

                if (!BEAST_EXPECT(waitShard(*db, 1) != std::nullopt))
                    return;

                for (std::uint32_t j = 0; j < ledgersPerShard; ++j)
                    checkLedger(data, *db, *data.ledgers_[j]);
            }

            BEAST_EXPECT(
                ripemd160File((path / "nudb.key").string()) == ripemd160Key);
            BEAST_EXPECT(
                ripemd160File((path / "nudb.dat").string()) == ripemd160Dat);
        }
    }

    void
    testImportNodeStore(std::uint64_t const seedValue)
    {
        testcase("Import node store");

        using namespace test::jtx;

        beast::temp_dir shardDir;
        {
            beast::temp_dir nodeDir;
            Env env{*this, testConfig(shardDir.path(), nodeDir.path())};
            DatabaseShard* db = env.app().getShardStore();
            Database& ndb = env.app().getNodeStore();
            BEAST_EXPECT(db);

            TestData data(seedValue, 4, 2);
            if (!BEAST_EXPECT(data.makeLedgers(env)))
                return;

            for (std::uint32_t i = 0; i < 2 * ledgersPerShard; ++i)
                BEAST_EXPECT(saveLedger(ndb, *data.ledgers_[i]));

            BEAST_EXPECT(db->getShardInfo()->finalized().empty());
            db->importDatabase(ndb);
            for (std::uint32_t i = 1; i <= 2; ++i)
                waitShard(*db, i);

            auto const finalShards{db->getShardInfo()->finalized()};
            for (std::uint32_t shardIndex : {1, 2})
                BEAST_EXPECT(boost::icl::contains(finalShards, shardIndex));
        }
        {
            Env env{*this, testConfig(shardDir.path())};
            DatabaseShard* db = env.app().getShardStore();
            BEAST_EXPECT(db);

            TestData data(seedValue, 4, 2);
            if (!BEAST_EXPECT(data.makeLedgers(env)))
                return;

            for (std::uint32_t i = 1; i <= 2; ++i)
                waitShard(*db, i);

            auto const finalShards{db->getShardInfo()->finalized()};
            for (std::uint32_t shardIndex : {1, 2})
                BEAST_EXPECT(boost::icl::contains(finalShards, shardIndex));

            for (std::uint32_t i = 0; i < 2 * ledgersPerShard; ++i)
                checkLedger(data, *db, *data.ledgers_[i]);
        }
    }

    void
    testImportWithOnlineDelete(std::uint64_t const seedValue)
    {
        testcase("Import node store with online delete");

        using namespace test::jtx;
        using test::CaptureLogs;

        beast::temp_dir shardDir;
        beast::temp_dir nodeDir;
        std::string capturedLogs;

        {
            auto c = testConfig(shardDir.path(), nodeDir.path());
            auto& section = c->section(ConfigSection::nodeDatabase());
            section.set("online_delete", "550");
            section.set("advisory_delete", "1");

            // Adjust the log level to capture relevant output
            c->section(SECTION_RPC_STARTUP)
                .append(
                    "{ \"command\": \"log_level\", \"severity\": \"trace\" "
                    "}");

            std::unique_ptr<Logs> logs(new CaptureLogs(&capturedLogs));
            Env env{*this, std::move(c), std::move(logs)};

            DatabaseShard* db = env.app().getShardStore();
            Database& ndb = env.app().getNodeStore();
            BEAST_EXPECT(db);

            // Create some ledgers for the shard store to import
            auto const shardCount = 5;
            TestData data(seedValue, 4, shardCount);
            if (!BEAST_EXPECT(data.makeLedgers(env)))
                return;

            auto& store = env.app().getSHAMapStore();
            auto lastRotated = store.getLastRotated();

            // Start the import
            db->importDatabase(ndb);

            while (!db->getDatabaseImportSequence())
            {
                // Wait until the import starts
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            // Enable online deletion now that the import has started
            store.setCanDelete(std::numeric_limits<std::uint32_t>::max());

            auto pauseVerifier = std::thread([lastRotated, &store, db, this] {
                while (true)
                {
                    // Make sure database rotations dont interfere
                    // with the import

                    if (store.getLastRotated() != lastRotated)
                    {
                        // A rotation occurred during shard import. Not
                        // necessarily an error

                        auto const ledgerSeq = db->getDatabaseImportSequence();
                        BEAST_EXPECT(!ledgerSeq || ledgerSeq >= lastRotated);

                        break;
                    }
                }
            });

            auto join = [&pauseVerifier]() {
                if (pauseVerifier.joinable())
                    pauseVerifier.join();
            };

            // Create more ledgers to trigger online deletion
            data = TestData(seedValue * 2);
            if (!BEAST_EXPECT(data.makeLedgers(env, shardCount)))
            {
                join();
                return;
            }

            join();
            BEAST_EXPECT(store.getLastRotated() != lastRotated);
        }

        // Database rotation should have been postponed at some
        // point during the import
        auto const expectedLogMessage =
            "rotation would interfere with ShardStore import";
        BEAST_EXPECT(
            capturedLogs.find(expectedLogMessage) != std::string::npos);
    }

    void
    testImportWithHistoricalPaths(std::uint64_t const seedValue)
    {
        testcase("Import with historical paths");

        using namespace test::jtx;

        // Test importing with multiple historical paths
        {
            beast::temp_dir shardDir;
            std::array<beast::temp_dir, 4> historicalDirs;
            std::array<boost::filesystem::path, 4> historicalPaths;

            std::transform(
                historicalDirs.begin(),
                historicalDirs.end(),
                historicalPaths.begin(),
                [](const beast::temp_dir& dir) { return dir.path(); });

            beast::temp_dir nodeDir;
            auto c = testConfig(shardDir.path(), nodeDir.path());

            auto& historyPaths = c->section(SECTION_HISTORICAL_SHARD_PATHS);
            historyPaths.append(
                {historicalPaths[0].string(),
                 historicalPaths[1].string(),
                 historicalPaths[2].string(),
                 historicalPaths[3].string()});

            Env env{*this, std::move(c)};
            DatabaseShard* db = env.app().getShardStore();
            Database& ndb = env.app().getNodeStore();
            BEAST_EXPECT(db);

            auto const shardCount = 4;

            TestData data(seedValue, 4, shardCount);
            if (!BEAST_EXPECT(data.makeLedgers(env)))
                return;

            for (std::uint32_t i = 0; i < shardCount * ledgersPerShard; ++i)
                BEAST_EXPECT(saveLedger(ndb, *data.ledgers_[i]));

            BEAST_EXPECT(db->getShardInfo()->finalized().empty());

            db->importDatabase(ndb);
            for (std::uint32_t i = 1; i <= shardCount; ++i)
                waitShard(*db, i);

            auto const final{db->getShardInfo()->finalized()};
            for (std::uint32_t shardIndex : {1, 2, 3, 4})
                BEAST_EXPECT(boost::icl::contains(final, shardIndex));

            auto const mainPathCount = std::distance(
                boost::filesystem::directory_iterator(shardDir.path()),
                boost::filesystem::directory_iterator());

            // Only the two most recent shards
            // should be stored at the main path
            BEAST_EXPECT(mainPathCount == 2);

            auto const historicalPathCount = std::accumulate(
                historicalPaths.begin(),
                historicalPaths.end(),
                0,
                [](int const sum, boost::filesystem::path const& path) {
                    return sum +
                        std::distance(
                               boost::filesystem::directory_iterator(path),
                               boost::filesystem::directory_iterator());
                });

            // All historical shards should be stored
            // at historical paths
            BEAST_EXPECT(historicalPathCount == shardCount - 2);
        }

        // Test importing with a single historical path
        {
            beast::temp_dir shardDir;
            beast::temp_dir historicalDir;
            beast::temp_dir nodeDir;

            auto c = testConfig(shardDir.path(), nodeDir.path());

            auto& historyPaths = c->section(SECTION_HISTORICAL_SHARD_PATHS);
            historyPaths.append({historicalDir.path()});

            Env env{*this, std::move(c)};
            DatabaseShard* db = env.app().getShardStore();
            Database& ndb = env.app().getNodeStore();
            BEAST_EXPECT(db);

            auto const shardCount = 4;

            TestData data(seedValue * 2, 4, shardCount);
            if (!BEAST_EXPECT(data.makeLedgers(env)))
                return;

            for (std::uint32_t i = 0; i < shardCount * ledgersPerShard; ++i)
                BEAST_EXPECT(saveLedger(ndb, *data.ledgers_[i]));

            BEAST_EXPECT(db->getShardInfo()->finalized().empty());

            db->importDatabase(ndb);
            for (std::uint32_t i = 1; i <= shardCount; ++i)
                waitShard(*db, i);

            auto const finalShards{db->getShardInfo()->finalized()};
            for (std::uint32_t shardIndex : {1, 2, 3, 4})
                BEAST_EXPECT(boost::icl::contains(finalShards, shardIndex));

            auto const mainPathCount = std::distance(
                boost::filesystem::directory_iterator(shardDir.path()),
                boost::filesystem::directory_iterator());

            // Only the two most recent shards
            // should be stored at the main path
            BEAST_EXPECT(mainPathCount == 2);

            auto const historicalPathCount = std::distance(
                boost::filesystem::directory_iterator(historicalDir.path()),
                boost::filesystem::directory_iterator());

            // All historical shards should be stored
            // at historical paths
            BEAST_EXPECT(historicalPathCount == shardCount - 2);
        }
    }

    void
    testPrepareWithHistoricalPaths(std::uint64_t const seedValue)
    {
        testcase("Prepare with historical paths");

        using namespace test::jtx;

        // Create the primary shard directory
        beast::temp_dir primaryDir;
        auto config{testConfig(primaryDir.path())};

        // Create four historical directories
        std::array<beast::temp_dir, 4> historicalDirs;
        {
            auto& paths{config->section(SECTION_HISTORICAL_SHARD_PATHS)};
            for (auto const& dir : historicalDirs)
                paths.append(dir.path());
        }

        Env env{*this, std::move(config)};

        // Create some shards
        std::uint32_t constexpr numShards{4};
        TestData data(seedValue, 4, numShards);
        if (!BEAST_EXPECT(data.makeLedgers(env)))
            return;

        auto shardStore{env.app().getShardStore()};
        BEAST_EXPECT(shardStore);

        for (auto i = 0; i < numShards; ++i)
        {
            auto const shardIndex{createShard(data, *shardStore, numShards)};
            if (!BEAST_EXPECT(
                    shardIndex && *shardIndex >= 1 && *shardIndex <= numShards))
            {
                return;
            }
        }

        {
            // Confirm finalized shards are in the shard store
            auto const finalized{shardStore->getShardInfo()->finalized()};
            BEAST_EXPECT(boost::icl::length(finalized) == numShards);
            BEAST_EXPECT(boost::icl::first(finalized) == 1);
            BEAST_EXPECT(boost::icl::last(finalized) == numShards);
        }

        using namespace boost::filesystem;
        auto const dirContains = [](beast::temp_dir const& dir,
                                    std::uint32_t shardIndex) {
            boost::filesystem::path const path(std::to_string(shardIndex));
            for (auto const& it : directory_iterator(dir.path()))
                if (boost::filesystem::path(it).stem() == path)
                    return true;
            return false;
        };
        auto const historicalDirsContains = [&](std::uint32_t shardIndex) {
            for (auto const& dir : historicalDirs)
                if (dirContains(dir, shardIndex))
                    return true;
            return false;
        };

        // Confirm two most recent shards are in the primary shard directory
        for (auto const shardIndex : {numShards - 1, numShards})
        {
            BEAST_EXPECT(dirContains(primaryDir, shardIndex));
            BEAST_EXPECT(!historicalDirsContains(shardIndex));
        }

        // Confirm remaining shards are in the historical shard directories
        for (auto shardIndex = 1; shardIndex < numShards - 1; ++shardIndex)
        {
            BEAST_EXPECT(!dirContains(primaryDir, shardIndex));
            BEAST_EXPECT(historicalDirsContains(shardIndex));
        }

        // Create some more shards to exercise recent shard rotation
        data = TestData(seedValue * 2, 4, numShards);
        if (!BEAST_EXPECT(data.makeLedgers(env, numShards)))
            return;

        for (auto i = 0; i < numShards; ++i)
        {
            auto const shardIndex{
                createShard(data, *shardStore, numShards * 2, numShards)};
            if (!BEAST_EXPECT(
                    shardIndex && *shardIndex >= numShards + 1 &&
                    *shardIndex <= numShards * 2))
            {
                return;
            }
        }

        {
            // Confirm finalized shards are in the shard store
            auto const finalized{shardStore->getShardInfo()->finalized()};
            BEAST_EXPECT(boost::icl::length(finalized) == numShards * 2);
            BEAST_EXPECT(boost::icl::first(finalized) == 1);
            BEAST_EXPECT(boost::icl::last(finalized) == numShards * 2);
        }

        // Confirm two most recent shards are in the primary shard directory
        for (auto const shardIndex : {numShards * 2 - 1, numShards * 2})
        {
            BEAST_EXPECT(dirContains(primaryDir, shardIndex));
            BEAST_EXPECT(!historicalDirsContains(shardIndex));
        }

        // Confirm remaining shards are in the historical shard directories
        for (auto shardIndex = 1; shardIndex < numShards * 2 - 1; ++shardIndex)
        {
            BEAST_EXPECT(!dirContains(primaryDir, shardIndex));
            BEAST_EXPECT(historicalDirsContains(shardIndex));
        }
    }

    void
    testOpenShardManagement(std::uint64_t const seedValue)
    {
        testcase("Open shard management");

        using namespace test::jtx;

        beast::temp_dir shardDir;
        Env env{*this, testConfig(shardDir.path())};

        auto shardStore{env.app().getShardStore()};
        BEAST_EXPECT(shardStore);

        // Create one shard more than the open final limit
        auto const openFinalLimit{env.app().config().getValueFor(
            SizedItem::openFinalLimit, std::nullopt)};
        auto const numShards{openFinalLimit + 1};

        TestData data(seedValue, 2, numShards);
        if (!BEAST_EXPECT(data.makeLedgers(env)))
            return;

        BEAST_EXPECT(shardStore->getShardInfo()->finalized().empty());

        int oldestShardIndex{-1};
        for (auto i = 0; i < numShards; ++i)
        {
            auto shardIndex{createShard(data, *shardStore, numShards)};
            if (!BEAST_EXPECT(
                    shardIndex && *shardIndex >= 1 && *shardIndex <= numShards))
            {
                return;
            }

            BEAST_EXPECT(boost::icl::contains(
                shardStore->getShardInfo()->finalized(), *shardIndex));

            if (oldestShardIndex == -1)
                oldestShardIndex = *shardIndex;
        }

        // The number of open shards exceeds the open limit by one.
        // A sweep will close enough shards to be within the limit.
        shardStore->sweep();

        // Read from the closed shard and automatically open it
        auto const ledgerSeq{shardStore->lastLedgerSeq(oldestShardIndex)};
        auto const index{ledgerSeq - ledgersPerShard - 1};
        BEAST_EXPECT(shardStore->fetchNodeObject(
            data.ledgers_[index]->info().hash, ledgerSeq));
    }

    void
    testShardInfo(std::uint64_t const seedValue)
    {
        testcase("Shard info");

        using namespace test::jtx;
        beast::temp_dir shardDir;
        Env env{*this, testConfig(shardDir.path())};

        auto shardStore{env.app().getShardStore()};
        BEAST_EXPECT(shardStore);

        // Check shard store is empty
        {
            auto const shardInfo{shardStore->getShardInfo()};
            BEAST_EXPECT(
                shardInfo->msgTimestamp().time_since_epoch().count() == 0);
            BEAST_EXPECT(shardInfo->finalizedToString().empty());
            BEAST_EXPECT(shardInfo->finalized().empty());
            BEAST_EXPECT(shardInfo->incompleteToString().empty());
            BEAST_EXPECT(shardInfo->incomplete().empty());
        }

        // Create an incomplete shard with index 1
        TestData data(seedValue, dataSizeMax, 2);
        if (!BEAST_EXPECT(data.makeLedgers(env)))
            return;
        if (!BEAST_EXPECT(shardStore->prepareLedger(2 * ledgersPerShard)))
            return;

        // Check shard is incomplete
        {
            auto const shardInfo{shardStore->getShardInfo()};
            BEAST_EXPECT(shardInfo->finalizedToString().empty());
            BEAST_EXPECT(shardInfo->finalized().empty());
            BEAST_EXPECT(shardInfo->incompleteToString() == "1:0");
            BEAST_EXPECT(
                shardInfo->incomplete().find(1) !=
                shardInfo->incomplete().end());
        }

        // Finalize the shard
        {
            auto shardIndex{createShard(data, *shardStore)};
            if (!BEAST_EXPECT(shardIndex && *shardIndex == 1))
                return;
        }

        // Check shard is finalized
        {
            auto const shardInfo{shardStore->getShardInfo()};
            BEAST_EXPECT(shardInfo->finalizedToString() == "1");
            BEAST_EXPECT(boost::icl::contains(shardInfo->finalized(), 1));
            BEAST_EXPECT(shardInfo->incompleteToString().empty());
            BEAST_EXPECT(shardInfo->incomplete().empty());
            BEAST_EXPECT(!shardInfo->update(1, ShardState::finalized, 0));
            BEAST_EXPECT(shardInfo->setFinalizedFromString("2"));
            BEAST_EXPECT(shardInfo->finalizedToString() == "2");
            BEAST_EXPECT(boost::icl::contains(shardInfo->finalized(), 2));
        }

        // Create an incomplete shard with index 2
        if (!BEAST_EXPECT(shardStore->prepareLedger(3 * ledgersPerShard)))
            return;

        // Store 10 percent of the ledgers
        for (std::uint32_t i = 0; i < (ledgersPerShard / 10); ++i)
        {
            auto const ledgerSeq{
                shardStore->prepareLedger(3 * ledgersPerShard)};
            if (!BEAST_EXPECT(ledgerSeq != std::nullopt))
                return;

            auto const arrInd{*ledgerSeq - ledgersPerShard - 1};
            if (!BEAST_EXPECT(saveLedger(*shardStore, *data.ledgers_[arrInd])))
                return;

            shardStore->setStored(data.ledgers_[arrInd]);
        }

        auto const shardInfo{shardStore->getShardInfo()};
        BEAST_EXPECT(shardInfo->incompleteToString() == "2:10");
        BEAST_EXPECT(
            shardInfo->incomplete().find(2) != shardInfo->incomplete().end());

        auto const timeStamp{env.app().timeKeeper().now()};
        shardInfo->setMsgTimestamp(timeStamp);
        BEAST_EXPECT(timeStamp == shardInfo->msgTimestamp());

        // Check message
        auto const msg{shardInfo->makeMessage(env.app())};
        Serializer s;
        s.add32(HashPrefix::shardInfo);

        BEAST_EXPECT(msg.timestamp() != 0);
        s.add32(msg.timestamp());

        // Verify incomplete shard
        {
            BEAST_EXPECT(msg.incomplete_size() == 1);

            auto const& incomplete{msg.incomplete(0)};
            BEAST_EXPECT(incomplete.shardindex() == 2);
            s.add32(incomplete.shardindex());

            BEAST_EXPECT(
                static_cast<ShardState>(incomplete.state()) ==
                ShardState::acquire);
            s.add32(incomplete.state());

            BEAST_EXPECT(incomplete.has_progress());
            BEAST_EXPECT(incomplete.progress() == 10);
            s.add32(incomplete.progress());
        }

        // Verify finalized shard
        BEAST_EXPECT(msg.has_finalized());
        BEAST_EXPECT(msg.finalized() == "1");
        s.addRaw(msg.finalized().data(), msg.finalized().size());

        // Verify public key
        auto slice{makeSlice(msg.publickey())};
        BEAST_EXPECT(publicKeyType(slice));

        // Verify signature
        BEAST_EXPECT(verify(
            PublicKey(slice), s.slice(), makeSlice(msg.signature()), false));

        BEAST_EXPECT(msg.peerchain_size() == 0);
    }

    void
    testRelationalDBInterfaceSqlite(std::uint64_t const seedValue)
    {
        testcase("Relational DB Interface SQLite");

        using namespace test::jtx;

        beast::temp_dir shardDir;
        Env env{*this, testConfig(shardDir.path())};

        auto shardStore{env.app().getShardStore()};
        BEAST_EXPECT(shardStore);

        auto const shardCount = 3;
        TestData data(seedValue, 3, shardCount);
        if (!BEAST_EXPECT(data.makeLedgers(env)))
            return;

        BEAST_EXPECT(shardStore->getShardInfo()->finalized().empty());
        BEAST_EXPECT(shardStore->getShardInfo()->incompleteToString().empty());

        auto rdb = dynamic_cast<RelationalDBInterfaceSqlite*>(
            &env.app().getRelationalDBInterface());

        BEAST_EXPECT(rdb);

        for (std::uint32_t i = 0; i < shardCount; ++i)
        {
            // Populate the shard store

            auto n = createShard(data, *shardStore, shardCount);
            if (!BEAST_EXPECT(n && *n >= 1 && *n <= shardCount))
                return;
        }

        // Close these databases to force the RelationalDBInterfaceSqlite
        // to use the shard databases and lookup tables.
        rdb->closeLedgerDB();
        rdb->closeTransactionDB();

        // Lambda for comparing Ledger objects
        auto infoCmp = [](auto const& a, auto const& b) {
            return a.hash == b.hash && a.txHash == b.txHash &&
                a.accountHash == b.accountHash &&
                a.parentHash == b.parentHash && a.drops == b.drops &&
                a.accepted == b.accepted && a.closeFlags == b.closeFlags &&
                a.closeTimeResolution == b.closeTimeResolution &&
                a.closeTime == b.closeTime;
        };

        for (auto const& ledger : data.ledgers_)
        {
            // Compare each test ledger to the data retrieved
            // from the RelationalDBInterfaceSqlite class

            if (shardStore->seqToShardIndex(ledger->seq()) <
                    shardStore->earliestShardIndex() ||
                ledger->info().seq < shardStore->earliestLedgerSeq())
                continue;

            auto info = rdb->getLedgerInfoByHash(ledger->info().hash);

            BEAST_EXPECT(info);
            BEAST_EXPECT(infoCmp(*info, ledger->info()));

            for (auto const& transaction : ledger->txs)
            {
                // Compare each test transaction to the data
                // retrieved from the RelationalDBInterfaceSqlite
                // class

                error_code_i error{rpcSUCCESS};

                auto reference = rdb->getTransaction(
                    transaction.first->getTransactionID(), {}, error);

                BEAST_EXPECT(error == rpcSUCCESS);
                if (!BEAST_EXPECT(reference.index() == 0))
                    continue;

                auto txn = std::get<0>(reference).first->getSTransaction();

                BEAST_EXPECT(
                    transaction.first->getFullText() == txn->getFullText());
            }
        }

        // Create additional ledgers to test a pathway in
        // 'ripple::saveLedgerMeta' wherein fetching the
        // accepted ledger fails
        data = TestData(seedValue * 2, 4, 1);
        if (!BEAST_EXPECT(data.makeLedgers(env, shardCount)))
            return;
    }

public:
    DatabaseShard_test() : journal_("DatabaseShard_test", *this)
    {
    }

    void
    run() override
    {
        auto seedValue = [] {
            static std::uint64_t seedValue = 41;
            seedValue += 10;
            return seedValue;
        };

        testStandalone();
        testCreateShard(seedValue());
        testReopenDatabase(seedValue());
        testGetFinalShards(seedValue());
        testPrepareShards(seedValue());
        testImportShard(seedValue());
        testCorruptedDatabase(seedValue());
        testIllegalFinalKey(seedValue());
        testDeterministicShard(seedValue());
        testImportNodeStore(seedValue());
        testImportWithOnlineDelete(seedValue());
        testImportWithHistoricalPaths(seedValue());
        testPrepareWithHistoricalPaths(seedValue());
        testOpenShardManagement(seedValue());
        testShardInfo(seedValue());
        testRelationalDBInterfaceSqlite(seedValue());
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(DatabaseShard, NodeStore, ripple);

}  // namespace NodeStore
}  // namespace ripple
