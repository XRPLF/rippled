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
#include <ripple/nodestore/Manager.h>
#include <ripple/nodestore/impl/DecodedBlob.h>
#include <ripple/nodestore/impl/EncodedBlob.h>
#include <ripple/nodestore/impl/Shard.h>
#include <chrono>
#include <test/jtx.h>
#include <test/nodestore/TestBase.h>

namespace ripple {
namespace NodeStore {

// Tests DatabaseShard class
//
class DatabaseShard_test : public TestBase
{
    static constexpr std::uint32_t maxSizeGb = 10;
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
         * number of acoounts: source and destinations, which participate in
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
        makeLedgers(test::jtx::Env& env_)
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

            for (std::uint32_t i = 0; i < ledgersPerShard * nShards_; ++i)
            {
                makeLedgerData(env_, i);
                if (!env_.close())
                    return false;
                std::shared_ptr<const Ledger> ledger =
                    env_.app().getLedgerMaster().getClosedLedger();
                if (ledger->info().seq != i + ledgersPerShard + 1)
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
            Serializer s(128);
            s.add32(HashPrefix::ledgerMaster);
            addRaw(ledger.info(), s);
            db.store(
                hotLEDGER,
                std::move(s.modData()),
                ledger.info().hash,
                ledger.info().seq);
        }

        // Store the state map
        auto visitAcc = [&](SHAMapAbstractNode& node) {
            Serializer s;
            node.addRaw(s, snfPREFIX);
            db.store(
                node.getType() == SHAMapAbstractNode::TNType::tnINNER
                    ? hotUNKNOWN
                    : hotACCOUNT_NODE,
                std::move(s.modData()),
                node.getNodeHash().as_uint256(),
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
        auto visitTx = [&](SHAMapAbstractNode& node) {
            Serializer s;
            node.addRaw(s, snfPREFIX);
            db.store(
                node.getType() == SHAMapAbstractNode::TNType::tnINNER
                    ? hotUNKNOWN
                    : hotTRANSACTION_NODE,
                std::move(s.modData()),
                node.getNodeHash().as_uint256(),
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
        auto fcompAcc = [&](SHAMapAbstractNode& node) -> bool {
            Serializer s;
            node.addRaw(s, snfPREFIX);
            auto nSrc{NodeObject::createObject(
                node.getType() == SHAMapAbstractNode::TNType::tnINNER
                    ? hotUNKNOWN
                    : hotACCOUNT_NODE,
                std::move(s.modData()),
                node.getNodeHash().as_uint256())};
            if (!BEAST_EXPECT(nSrc))
                return false;

            auto nDst =
                db.fetch(node.getNodeHash().as_uint256(), ledger.info().seq);
            if (!BEAST_EXPECT(nDst))
                return false;

            BEAST_EXPECT(isSame(nSrc, nDst));

            return true;
        };
        if (ledger.stateMap().getHash().isNonZero())
            ledger.stateMap().snapShot(false)->visitNodes(fcompAcc);

        auto fcompTx = [&](SHAMapAbstractNode& node) -> bool {
            Serializer s;
            node.addRaw(s, snfPREFIX);
            auto nSrc{NodeObject::createObject(
                node.getType() == SHAMapAbstractNode::TNType::tnINNER
                    ? hotUNKNOWN
                    : hotTRANSACTION_NODE,
                std::move(s.modData()),
                node.getNodeHash().as_uint256())};
            if (!BEAST_EXPECT(nSrc))
                return false;

            auto nDst =
                db.fetch(node.getNodeHash().as_uint256(), ledger.info().seq);
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
        std::string const& testName,
        std::string const& backendType,
        std::string const& shardDir,
        std::string const& nodeDir = std::string())
    {
        using namespace test::jtx;

        if (testName != "")
        {
            std::string caseName =
                "DatabaseShard " + testName + " with backend " + backendType;
            testcase(caseName);
        }

        return envconfig([&](std::unique_ptr<Config> cfg) {
            cfg->overwrite(ConfigSection::shardDatabase(), "type", backendType);
            cfg->overwrite(ConfigSection::shardDatabase(), "path", shardDir);
            cfg->overwrite(
                ConfigSection::shardDatabase(),
                "max_size_gb",
                std::to_string(maxSizeGb));
            cfg->overwrite(
                ConfigSection::shardDatabase(),
                "ledgers_per_shard",
                std::to_string(ledgersPerShard));
            cfg->overwrite(
                ConfigSection::shardDatabase(),
                "earliest_seq",
                std::to_string(earliestSeq));
            cfg->overwrite(ConfigSection::nodeDatabase(), "type", backendType);
            cfg->overwrite(
                ConfigSection::nodeDatabase(),
                "max_size_gb",
                std::to_string(maxSizeGb));
            cfg->overwrite(
                ConfigSection::nodeDatabase(),
                "earliest_seq",
                std::to_string(earliestSeq));
            if (nodeDir.empty())
                cfg->overwrite(
                    ConfigSection::nodeDatabase(), "path", defNodeDir.path());
            else
                cfg->overwrite(ConfigSection::nodeDatabase(), "path", nodeDir);
            return cfg;
        });
    }

    std::optional<int>
    waitShard(
        DatabaseShard& db,
        int shardNumber,
        std::chrono::seconds timeout = shardStoreTimeout)
    {
        RangeSet<std::uint32_t> rs;
        auto start = std::chrono::system_clock::now();
        auto end = start + timeout;
        while (!from_string(rs, db.getCompleteShards()) ||
               !boost::icl::contains(rs, shardNumber))
        {
            if (!BEAST_EXPECT(std::chrono::system_clock::now() < end))
                return {};
            std::this_thread::yield();
        }

        return shardNumber;
    }

    std::optional<int>
    createShard(TestData& data, DatabaseShard& db, int maxShardNumber)
    {
        int shardNumber = -1;

        for (std::uint32_t i = 0; i < ledgersPerShard; ++i)
        {
            auto ind = db.prepareLedger((maxShardNumber + 1) * ledgersPerShard);
            if (!BEAST_EXPECT(ind != boost::none))
                return {};
            shardNumber = db.seqToShardIndex(*ind);
            int arrInd = *ind - ledgersPerShard - 1;
            BEAST_EXPECT(
                arrInd >= 0 && arrInd < maxShardNumber * ledgersPerShard);
            BEAST_EXPECT(saveLedger(db, *data.ledgers_[arrInd]));
            if (arrInd % ledgersPerShard == (ledgersPerShard - 1))
            {
                uint256 const finalKey_{0};
                Serializer s;
                s.add32(Shard::version);
                s.add32(db.firstLedgerSeq(shardNumber));
                s.add32(db.lastLedgerSeq(shardNumber));
                s.addRaw(data.ledgers_[arrInd]->info().hash.data(), 256 / 8);
                db.store(hotUNKNOWN, std::move(s.modData()), finalKey_, *ind);
            }
            db.setStored(data.ledgers_[arrInd]);
        }

        return waitShard(db, shardNumber);
    }

    void
    testStandalone(std::string const& backendType)
    {
        using namespace test::jtx;

        beast::temp_dir shardDir;
        Env env{*this, testConfig("standalone", backendType, shardDir.path())};
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
    testCreateShard(
        std::string const& backendType,
        std::uint64_t const seedValue)
    {
        using namespace test::jtx;

        beast::temp_dir shardDir;
        Env env{*this, testConfig("createShard", backendType, shardDir.path())};
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
    testReopenDatabase(
        std::string const& backendType,
        std::uint64_t const seedValue)
    {
        using namespace test::jtx;

        beast::temp_dir shardDir;
        {
            Env env{
                *this,
                testConfig("reopenDatabase", backendType, shardDir.path())};
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
            Env env{*this, testConfig("", backendType, shardDir.path())};
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
    testGetCompleteShards(
        std::string const& backendType,
        std::uint64_t const seedValue)
    {
        using namespace test::jtx;

        beast::temp_dir shardDir;
        Env env{
            *this,
            testConfig("getCompleteShards", backendType, shardDir.path())};
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
    testPrepareShard(
        std::string const& backendType,
        std::uint64_t const seedValue)
    {
        using namespace test::jtx;

        beast::temp_dir shardDir;
        Env env{
            *this, testConfig("prepareShard", backendType, shardDir.path())};
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
                db->prepareShard(n);
                bitMask |= 1ll << n;
            }
            BEAST_EXPECT(db->getPreShards() == bitmask2Rangeset(bitMask));
        }

        // test illegal cases
        // adding shards with too large number
        db->prepareShard(0);
        BEAST_EXPECT(db->getPreShards() == bitmask2Rangeset(bitMask));
        db->prepareShard(nTestShards + 1);
        BEAST_EXPECT(db->getPreShards() == bitmask2Rangeset(bitMask));
        db->prepareShard(nTestShards + 2);
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
    testImportShard(
        std::string const& backendType,
        std::uint64_t const seedValue)
    {
        using namespace test::jtx;

        beast::temp_dir importDir;
        TestData data(seedValue, 2);

        {
            Env env{
                *this,
                testConfig("importShard", backendType, importDir.path())};
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
            Env env{*this, testConfig("", backendType, shardDir.path())};
            DatabaseShard* db = env.app().getShardStore();
            BEAST_EXPECT(db);

            if (!BEAST_EXPECT(data.makeLedgers(env)))
                return;

            db->prepareShard(1);
            BEAST_EXPECT(db->getPreShards() == bitmask2Rangeset(2));
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
    testCorruptedDatabase(
        std::string const& backendType,
        std::uint64_t const seedValue)
    {
        using namespace test::jtx;

        beast::temp_dir shardDir;
        {
            TestData data(seedValue, 4, 2);
            {
                Env env{
                    *this,
                    testConfig(
                        "corruptedDatabase", backendType, shardDir.path())};
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
            path /= backendType + ".dat";

            FILE* f = fopen(path.string().c_str(), "r+b");
            if (!BEAST_EXPECT(f))
                return;
            char buf[256];
            beast::rngfill(buf, sizeof(buf), data.rng_);
            BEAST_EXPECT(fwrite(buf, 1, 256, f) == 256);
            fclose(f);
        }
        {
            Env env{*this, testConfig("", backendType, shardDir.path())};
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
    }

    void
    testIllegalFinalKey(
        std::string const& backendType,
        std::uint64_t const seedValue)
    {
        using namespace test::jtx;

        for (int i = 0; i < 5; ++i)
        {
            beast::temp_dir shardDir;
            {
                Env env{
                    *this,
                    testConfig(
                        (i == 0 ? "illegalFinalKey" : ""),
                        backendType,
                        shardDir.path())};
                DatabaseShard* db = env.app().getShardStore();
                BEAST_EXPECT(db);

                TestData data(seedValue + i, 2);
                if (!BEAST_EXPECT(data.makeLedgers(env)))
                    return;

                int shardNumber = -1;
                for (std::uint32_t j = 0; j < ledgersPerShard; ++j)
                {
                    auto ind = db->prepareLedger(2 * ledgersPerShard);
                    if (!BEAST_EXPECT(ind != boost::none))
                        return;
                    shardNumber = db->seqToShardIndex(*ind);
                    int arrInd = *ind - ledgersPerShard - 1;
                    BEAST_EXPECT(arrInd >= 0 && arrInd < ledgersPerShard);
                    BEAST_EXPECT(saveLedger(*db, *data.ledgers_[arrInd]));
                    if (arrInd % ledgersPerShard == (ledgersPerShard - 1))
                    {
                        uint256 const finalKey_{0};
                        Serializer s;
                        s.add32(Shard::version + (i == 0));
                        s.add32(db->firstLedgerSeq(shardNumber) + (i == 1));
                        s.add32(db->lastLedgerSeq(shardNumber) - (i == 3));
                        s.addRaw(
                            data.ledgers_[arrInd - (i == 4)]
                                ->info()
                                .hash.data(),
                            256 / 8);
                        db->store(
                            hotUNKNOWN,
                            std::move(s.modData()),
                            finalKey_,
                            *ind);
                    }
                    db->setStored(data.ledgers_[arrInd]);
                }

                if (i == 2)
                    waitShard(*db, shardNumber);
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
                Env env{*this, testConfig("", backendType, shardDir.path())};
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
    testImport(std::string const& backendType, std::uint64_t const seedValue)
    {
        using namespace test::jtx;

        beast::temp_dir shardDir;
        {
            beast::temp_dir nodeDir;
            Env env{
                *this,
                testConfig(
                    "import", backendType, shardDir.path(), nodeDir.path())};
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
            Env env{*this, testConfig("", backendType, shardDir.path())};
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
    testAll(std::string const& backendType)
    {
        std::uint64_t const seedValue = 51;
        testStandalone(backendType);
        testCreateShard(backendType, seedValue);
        testReopenDatabase(backendType, seedValue + 5);
        testGetCompleteShards(backendType, seedValue + 10);
        testPrepareShard(backendType, seedValue + 20);
        testImportShard(backendType, seedValue + 30);
        testCorruptedDatabase(backendType, seedValue + 40);
        testIllegalFinalKey(backendType, seedValue + 50);
        testImport(backendType, seedValue + 60);
    }

public:
    DatabaseShard_test() : journal_("DatabaseShard_test", *this)
    {
    }

    void
    run() override
    {
        testAll("nudb");

#if RIPPLE_ROCKSDB_AVAILABLE
//      testAll ("rocksdb");
#endif

#if RIPPLE_ENABLE_SQLITE_BACKEND_TESTS
        testAll("sqlite");
#endif
    }
};

BEAST_DEFINE_TESTSUITE(DatabaseShard, NodeStore, ripple);

}  // namespace NodeStore
}  // namespace ripple
