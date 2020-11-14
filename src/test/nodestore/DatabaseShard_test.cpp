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
#include <ripple/beast/utility/temp_dir.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/nodestore/DummyScheduler.h>
#include <ripple/nodestore/impl/DecodedBlob.h>
#include <ripple/nodestore/impl/Shard.h>
#include <chrono>
#include <numeric>
#include <test/jtx.h>
#include <test/nodestore/TestBase.h>

namespace ripple {
namespace NodeStore {

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
        int nShards_;
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
            int nShards = 1)
            : rng_(seedValue), nShards_(nShards)
        {
            std::uint32_t n = 0;
            std::uint32_t nLedgers = ledgersPerShard * nShards;

            nAccounts_.reserve(nLedgers);
            payAccounts_.reserve(nLedgers);
            xrpAmount_.reserve(nLedgers);

            for (std::uint32_t i = 0; i < nLedgers; ++i)
            {
                int p;
                if (n >= 2)
                    p = rand_int(rng_, 2 * dataSize);
                else
                    p = 0;

                std::vector<std::pair<int, int>> pay;
                pay.reserve(p);

                for (int j = 0; j < p; ++j)
                {
                    int from, to;
                    do
                    {
                        from = rand_int(rng_, n - 1);
                        to = rand_int(rng_, n - 1);
                    } while (from == to);

                    pay.push_back(std::make_pair(from, to));
                }

                n += !rand_int(rng_, nLedgers / dataSize);

                if (n > accounts_.size())
                {
                    char str[9];
                    for (int j = 0; j < 8; ++j)
                        str[j] = 'a' + rand_int(rng_, 'z' - 'a');
                    str[8] = 0;
                    accounts_.emplace_back(str);
                }

                nAccounts_.push_back(n);
                payAccounts_.push_back(std::move(pay));
                xrpAmount_.push_back(rand_int(rng_, 90) + 10);
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

            for (std::uint32_t i = 0; i < ledgersPerShard * nShards_; ++i)
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
            getJson(
                LedgerFill{ledger, LedgerFill::full | LedgerFill::expand}) ==
            getJson(
                LedgerFill{*fetched, LedgerFill::full | LedgerFill::expand}));

        BEAST_EXPECT(
            getJson(
                LedgerFill{ledger, LedgerFill::full | LedgerFill::binary}) ==
            getJson(
                LedgerFill{*fetched, LedgerFill::full | LedgerFill::binary}));

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
        from_string(rs, set);
        return to_string(rs);
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
                "earliest_seq",
                std::to_string(earliestSeq));
            cfg->overwrite(
                ConfigSection::nodeDatabase(),
                "path",
                nodeDir.empty() ? defNodeDir.path() : nodeDir);
            return cfg;
        });
    }

    std::optional<int>
    waitShard(
        DatabaseShard& db,
        int shardIndex,
        std::chrono::seconds timeout = shardStoreTimeout)
    {
        RangeSet<std::uint32_t> rs;
        auto start = std::chrono::system_clock::now();
        auto end = start + timeout;
        while (!from_string(rs, db.getCompleteShards()) ||
               !boost::icl::contains(rs, shardIndex))
        {
            if (!BEAST_EXPECT(std::chrono::system_clock::now() < end))
                return {};
            std::this_thread::yield();
        }

        return shardIndex;
    }

    std::optional<int>
    createShard(
        TestData& data,
        DatabaseShard& db,
        int maxShardNumber = 1,
        int ledgerOffset = 0)
    {
        int shardIndex{-1};

        for (std::uint32_t i = 0; i < ledgersPerShard; ++i)
        {
            auto const ledgerSeq{
                db.prepareLedger((maxShardNumber + 1) * ledgersPerShard)};
            if (!BEAST_EXPECT(ledgerSeq != boost::none))
                return {};

            shardIndex = db.seqToShardIndex(*ledgerSeq);

            int const arrInd = *ledgerSeq - (ledgersPerShard * ledgerOffset) -
                ledgersPerShard - 1;
            BEAST_EXPECT(
                arrInd >= 0 && arrInd < maxShardNumber * ledgersPerShard);
            BEAST_EXPECT(saveLedger(db, *data.ledgers_[arrInd]));
            if (arrInd % ledgersPerShard == (ledgersPerShard - 1))
            {
                uint256 const finalKey_{0};
                Serializer s;
                s.add32(Shard::version);
                s.add32(db.firstLedgerSeq(shardIndex));
                s.add32(db.lastLedgerSeq(shardIndex));
                s.addRaw(data.ledgers_[arrInd]->info().hash.data(), 256 / 8);
                db.store(
                    hotUNKNOWN, std::move(s.modData()), finalKey_, *ledgerSeq);
            }
            db.setStored(data.ledgers_[arrInd]);
        }

        return waitShard(db, shardIndex);
    }

    void
    testStandalone()
    {
        testcase("Standalone");

        using namespace test::jtx;

        beast::temp_dir shardDir;
        Env env{*this, testConfig(shardDir.path())};
        DummyScheduler scheduler;
        RootStoppable parent("TestRootStoppable");

        std::unique_ptr<DatabaseShard> db =
            make_ShardStore(env.app(), parent, scheduler, 2, journal_);

        BEAST_EXPECT(db);
        BEAST_EXPECT(db->ledgersPerShard() == db->ledgersPerShardDefault);
        BEAST_EXPECT(db->init());
        BEAST_EXPECT(db->ledgersPerShard() == ledgersPerShard);
        BEAST_EXPECT(db->seqToShardIndex(ledgersPerShard + 1) == 1);
        BEAST_EXPECT(db->seqToShardIndex(2 * ledgersPerShard) == 1);
        BEAST_EXPECT(db->seqToShardIndex(2 * ledgersPerShard + 1) == 2);
        BEAST_EXPECT(
            db->earliestShardIndex() == (earliestSeq - 1) / ledgersPerShard);
        BEAST_EXPECT(db->firstLedgerSeq(1) == ledgersPerShard + 1);
        BEAST_EXPECT(db->lastLedgerSeq(1) == 2 * ledgersPerShard);
        BEAST_EXPECT(db->getRootDir().string() == shardDir.path());
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

            for (std::uint32_t i = 0; i < 2; ++i)
                if (!createShard(data, *db, 2))
                    return;
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
    testGetCompleteShards(std::uint64_t const seedValue)
    {
        testcase("Get complete shards");

        using namespace test::jtx;

        beast::temp_dir shardDir;
        Env env{*this, testConfig(shardDir.path())};
        DatabaseShard* db = env.app().getShardStore();
        BEAST_EXPECT(db);

        TestData data(seedValue, 2, nTestShards);
        if (!BEAST_EXPECT(data.makeLedgers(env)))
            return;

        BEAST_EXPECT(db->getCompleteShards() == "");

        std::uint64_t bitMask = 0;

        for (std::uint32_t i = 0; i < nTestShards; ++i)
        {
            auto n = createShard(data, *db, nTestShards);
            if (!BEAST_EXPECT(n && *n >= 1 && *n <= nTestShards))
                return;
            bitMask |= 1ll << *n;
            BEAST_EXPECT(db->getCompleteShards() == bitmask2Rangeset(bitMask));
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

        std::uint64_t bitMask = 0;
        BEAST_EXPECT(db->getPreShards() == "");

        for (std::uint32_t i = 0; i < nTestShards * 2; ++i)
        {
            std::uint32_t n = rand_int(data.rng_, nTestShards - 1) + 1;
            if (bitMask & (1ll << n))
            {
                db->removePreShard(n);
                bitMask &= ~(1ll << n);
            }
            else
            {
                db->prepareShards({n});
                bitMask |= 1ll << n;
            }
            BEAST_EXPECT(db->getPreShards() == bitmask2Rangeset(bitMask));
        }

        // test illegal cases
        // adding shards with too large number
        db->prepareShards({0});
        BEAST_EXPECT(db->getPreShards() == bitmask2Rangeset(bitMask));
        db->prepareShards({nTestShards + 1});
        BEAST_EXPECT(db->getPreShards() == bitmask2Rangeset(bitMask));
        db->prepareShards({nTestShards + 2});
        BEAST_EXPECT(db->getPreShards() == bitmask2Rangeset(bitMask));

        // create shards which are not prepared for import
        BEAST_EXPECT(db->getCompleteShards() == "");

        std::uint64_t bitMask2 = 0;

        for (std::uint32_t i = 0; i < nTestShards; ++i)
        {
            auto n = createShard(data, *db, nTestShards);
            if (!BEAST_EXPECT(n && *n >= 1 && *n <= nTestShards))
                return;
            bitMask2 |= 1ll << *n;
            BEAST_EXPECT(db->getPreShards() == bitmask2Rangeset(bitMask));
            BEAST_EXPECT(db->getCompleteShards() == bitmask2Rangeset(bitMask2));
            BEAST_EXPECT((bitMask & bitMask2) == 0);
            if ((bitMask | bitMask2) == ((1ll << nTestShards) - 1) << 1)
                break;
        }

        // try to create another shard
        BEAST_EXPECT(
            db->prepareLedger((nTestShards + 1) * ledgersPerShard) ==
            boost::none);
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

            db->prepareShards({1});
            BEAST_EXPECT(db->getPreShards() == bitmask2Rangeset(2));

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

                for (std::uint32_t i = 0; i < 2; ++i)
                    if (!BEAST_EXPECT(createShard(data, *db, 2)))
                        return;
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

        for (std::uint32_t i = 1; i <= 1; ++i)
            waitShard(*db, i);

        BEAST_EXPECT(db->getCompleteShards() == bitmask2Rangeset(0x2));

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
                    if (!BEAST_EXPECT(ledgerSeq != boost::none))
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
                    waitShard(*db, shardIndex);
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
                }

                BEAST_EXPECT(
                    db->getCompleteShards() ==
                    bitmask2Rangeset(i == 2 ? 2 : 0));
            }

            {
                Env env{*this, testConfig(shardDir.path())};
                DatabaseShard* db = env.app().getShardStore();
                BEAST_EXPECT(db);

                TestData data(seedValue + i, 2);
                if (!BEAST_EXPECT(data.makeLedgers(env)))
                    return;

                if (i == 2)
                    waitShard(*db, 1);

                BEAST_EXPECT(
                    db->getCompleteShards() ==
                    bitmask2Rangeset(i == 2 ? 2 : 0));

                if (i == 2)
                {
                    for (std::uint32_t j = 0; j < ledgersPerShard; ++j)
                        checkLedger(data, *db, *data.ledgers_[j]);
                }
            }
        }
    }

    void
    testImport(std::uint64_t const seedValue)
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

            BEAST_EXPECT(db->getCompleteShards() == bitmask2Rangeset(0));
            db->import(ndb);
            for (std::uint32_t i = 1; i <= 2; ++i)
                waitShard(*db, i);
            BEAST_EXPECT(db->getCompleteShards() == bitmask2Rangeset(0x6));
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

            BEAST_EXPECT(db->getCompleteShards() == bitmask2Rangeset(0x6));

            for (std::uint32_t i = 0; i < 2 * ledgersPerShard; ++i)
                checkLedger(data, *db, *data.ledgers_[i]);
        }
    }

    void
    testImportWithHistoricalPaths(std::uint64_t const seedValue)
    {
        testcase("Import with historical paths");

        using namespace test::jtx;

        // Test importing with multiple historical
        // paths
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

            auto const ledgerCount = 4;

            TestData data(seedValue, 4, ledgerCount);
            if (!BEAST_EXPECT(data.makeLedgers(env)))
                return;

            for (std::uint32_t i = 0; i < ledgerCount * ledgersPerShard; ++i)
                BEAST_EXPECT(saveLedger(ndb, *data.ledgers_[i]));

            BEAST_EXPECT(db->getCompleteShards() == bitmask2Rangeset(0));

            db->import(ndb);
            for (std::uint32_t i = 1; i <= ledgerCount; ++i)
                waitShard(*db, i);

            BEAST_EXPECT(db->getCompleteShards() == bitmask2Rangeset(0b11110));

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
            BEAST_EXPECT(historicalPathCount == ledgerCount - 2);
        }

        // Test importing with a single historical
        // path
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

            auto const ledgerCount = 4;

            TestData data(seedValue * 2, 4, ledgerCount);
            if (!BEAST_EXPECT(data.makeLedgers(env)))
                return;

            for (std::uint32_t i = 0; i < ledgerCount * ledgersPerShard; ++i)
                BEAST_EXPECT(saveLedger(ndb, *data.ledgers_[i]));

            BEAST_EXPECT(db->getCompleteShards() == bitmask2Rangeset(0));

            db->import(ndb);
            for (std::uint32_t i = 1; i <= ledgerCount; ++i)
                waitShard(*db, i);

            BEAST_EXPECT(db->getCompleteShards() == bitmask2Rangeset(0b11110));

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
            BEAST_EXPECT(historicalPathCount == ledgerCount - 2);
        }
    }

    void
    testPrepareWithHistoricalPaths(std::uint64_t const seedValue)
    {
        testcase("Prepare with historical paths");

        using namespace test::jtx;

        // Test importing with multiple historical
        // paths
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
            auto c = testConfig(shardDir.path());

            auto& historyPaths = c->section(SECTION_HISTORICAL_SHARD_PATHS);
            historyPaths.append(
                {historicalPaths[0].string(),
                 historicalPaths[1].string(),
                 historicalPaths[2].string(),
                 historicalPaths[3].string()});

            Env env{*this, std::move(c)};
            DatabaseShard* db = env.app().getShardStore();
            BEAST_EXPECT(db);

            auto const ledgerCount = 4;

            TestData data(seedValue, 4, ledgerCount);
            if (!BEAST_EXPECT(data.makeLedgers(env)))
                return;

            BEAST_EXPECT(db->getCompleteShards() == "");
            std::uint64_t bitMask = 0;

            // Add ten shards to the Shard Database
            for (std::uint32_t i = 0; i < ledgerCount; ++i)
            {
                auto n = createShard(data, *db, ledgerCount);
                if (!BEAST_EXPECT(n && *n >= 1 && *n <= ledgerCount))
                    return;
                bitMask |= 1ll << *n;
                BEAST_EXPECT(
                    db->getCompleteShards() == bitmask2Rangeset(bitMask));
            }

            auto mainPathCount = std::distance(
                boost::filesystem::directory_iterator(shardDir.path()),
                boost::filesystem::directory_iterator());

            // Only the two most recent shards
            // should be stored at the main path
            BEAST_EXPECT(mainPathCount == 2);

            // Confirm recent shard locations
            std::set<boost::filesystem::path> mainPathShards{
                shardDir.path() / boost::filesystem::path("3"),
                shardDir.path() / boost::filesystem::path("4")};
            std::set<boost::filesystem::path> actual(
                boost::filesystem::directory_iterator(shardDir.path()),
                boost::filesystem::directory_iterator());

            BEAST_EXPECT(mainPathShards == actual);

            const auto generateHistoricalStems = [&historicalPaths, &actual] {
                for (auto const& path : historicalPaths)
                {
                    for (auto const& shard :
                         boost::filesystem::directory_iterator(path))
                    {
                        actual.insert(boost::filesystem::path(shard).stem());
                    }
                }
            };

            // Confirm historical shard locations
            std::set<boost::filesystem::path> historicalPathShards;
            std::generate_n(
                std::inserter(
                    historicalPathShards, historicalPathShards.begin()),
                2,
                [n = 1]() mutable { return std::to_string(n++); });
            actual.clear();
            generateHistoricalStems();

            BEAST_EXPECT(historicalPathShards == actual);

            auto historicalPathCount = std::accumulate(
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
            BEAST_EXPECT(historicalPathCount == ledgerCount - 2);

            data = TestData(seedValue * 2, 4, ledgerCount);
            if (!BEAST_EXPECT(data.makeLedgers(env, ledgerCount)))
                return;

            // Add ten more shards to the Shard Database
            // to exercise recent shard rotation
            for (std::uint32_t i = 0; i < ledgerCount; ++i)
            {
                auto n = createShard(data, *db, ledgerCount * 2, ledgerCount);
                if (!BEAST_EXPECT(
                        n && *n >= 1 + ledgerCount && *n <= ledgerCount * 2))
                    return;
                bitMask |= 1ll << *n;
                BEAST_EXPECT(
                    db->getCompleteShards() == bitmask2Rangeset(bitMask));
            }

            mainPathCount = std::distance(
                boost::filesystem::directory_iterator(shardDir.path()),
                boost::filesystem::directory_iterator());

            // Only the two most recent shards
            // should be stored at the main path
            BEAST_EXPECT(mainPathCount == 2);

            // Confirm recent shard locations
            mainPathShards = {
                shardDir.path() / boost::filesystem::path("7"),
                shardDir.path() / boost::filesystem::path("8")};
            actual = {
                boost::filesystem::directory_iterator(shardDir.path()),
                boost::filesystem::directory_iterator()};

            BEAST_EXPECT(mainPathShards == actual);

            // Confirm historical shard locations
            historicalPathShards.clear();
            std::generate_n(
                std::inserter(
                    historicalPathShards, historicalPathShards.begin()),
                6,
                [n = 1]() mutable { return std::to_string(n++); });
            actual.clear();
            generateHistoricalStems();

            BEAST_EXPECT(historicalPathShards == actual);

            historicalPathCount = std::accumulate(
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
            BEAST_EXPECT(historicalPathCount == (ledgerCount * 2) - 2);
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
            SizedItem::openFinalLimit, boost::none)};
        auto const numShards{openFinalLimit + 1};

        TestData data(seedValue, 2, numShards);
        if (!BEAST_EXPECT(data.makeLedgers(env)))
            return;

        BEAST_EXPECT(shardStore->getCompleteShards().empty());

        int oldestShardIndex{-1};
        std::uint64_t bitMask{0};
        for (auto i = 0; i < numShards; ++i)
        {
            auto shardIndex{createShard(data, *shardStore, numShards)};
            if (!BEAST_EXPECT(
                    shardIndex && *shardIndex >= 1 && *shardIndex <= numShards))
                return;

            bitMask |= (1ll << *shardIndex);

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

public:
    DatabaseShard_test() : journal_("DatabaseShard_test", *this)
    {
    }

    void
    run() override
    {
        std::uint64_t const seedValue = 51;

        testStandalone();
        testCreateShard(seedValue);
        testReopenDatabase(seedValue + 10);
        testGetCompleteShards(seedValue + 20);
        testPrepareShards(seedValue + 30);
        testImportShard(seedValue + 40);
        testCorruptedDatabase(seedValue + 50);
        testIllegalFinalKey(seedValue + 60);
        testImport(seedValue + 70);
        testImportWithHistoricalPaths(seedValue + 80);
        testPrepareWithHistoricalPaths(seedValue + 90);
        testOpenShardManagement(seedValue + 100);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(DatabaseShard, NodeStore, ripple);

}  // namespace NodeStore
}  // namespace ripple
