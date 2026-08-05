#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/pay.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/app/main/NodeStoreScheduler.h>
#include <xrpld/app/misc/SHAMapStore.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/json/json_value.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/DatabaseRotating.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/detail/DatabaseRotatingImp.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/State.h>
#include <xrpl/shamap/Family.h>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include <soci/session.h>

#include <atomic>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace xrpl::test {

class SHAMapStore_test : public beast::unit_test::Suite
{
    static auto const kDeleteInterval = 8;

    static auto
    onlineDelete(std::unique_ptr<Config> cfg)
    {
        cfg->ledgerHistory = kDeleteInterval;
        auto& section = cfg->section(Sections::kNodeDatabase);
        section.set(Keys::kOnlineDelete, std::to_string(kDeleteInterval));
        return cfg;
    }

    static auto
    advisoryDelete(std::unique_ptr<Config> cfg)
    {
        cfg = onlineDelete(std::move(cfg));
        cfg->section(Sections::kNodeDatabase).set(Keys::kAdvisoryDelete, "1");
        return cfg;
    }

    static auto
    generationalDelete(std::unique_ptr<Config> cfg)
    {
        // The smallest budget: retirement runs on every rotation.
        cfg = onlineDelete(std::move(cfg));
        cfg->section(Sections::kNodeDatabase).set(Keys::kOnlineDeleteGenerations, "2");
        return cfg;
    }

    // On-disk NuDB config rooted at fixed paths, so the ring and state survive an Env
    // restart within a test.
    static std::unique_ptr<Config>
    diskConfig(std::string const& nodeDb, std::string const& stateDir, std::string const& budget)
    {
        return jtx::envconfig([&](std::unique_ptr<Config> cfg) {
            cfg = onlineDelete(std::move(cfg));
            auto& section = cfg->section(Sections::kNodeDatabase);
            section.set(Keys::kType, "NuDB");
            section.set(Keys::kPath, nodeDb);
            section.set(Keys::kOnlineDeleteGenerations, budget);
            cfg->legacy(Sections::kDatabasePath, stateDir);
            return cfg;
        });
    }

    // Close ledgers until the next rotation fires, asserting it landed where expected.
    void
    rotateOnce(jtx::Env& env, int& ledgerSeq)
    {
        auto& store = env.app().getSHAMapStore();
        auto const target = store.getLastRotated() + kDeleteInterval;
        while (ledgerSeq <= static_cast<int>(target))
        {
            env.close();
            ++ledgerSeq;
            store.rendezvous();
        }
        BEAST_EXPECT(store.getLastRotated() == target);
    }

    static bool
    goodLedger(jtx::Env& env, json::Value const& json, std::string ledgerID, bool checkDB = false)
    {
        auto good = json.isMember(jss::result) && !rpc::containsError(json[jss::result]) &&
            json[jss::result][jss::ledger][jss::ledger_index] == ledgerID;
        if (!good || !checkDB)
            return good;

        auto const seq = json[jss::result][jss::ledger_index].asUInt();

        std::optional<LedgerHeader> outInfo =
            env.app().getRelationalDatabase().getLedgerInfoByIndex(seq);
        if (!outInfo)
            return false;
        LedgerHeader const& info = outInfo.value();

        std::string const outHash = to_string(info.hash);
        LedgerIndex const outSeq = info.seq;
        std::string const outParentHash = to_string(info.parentHash);
        std::string const outDrops = to_string(info.drops);
        std::uint64_t const outCloseTime = info.closeTime.time_since_epoch().count();
        std::uint64_t const outParentCloseTime = info.parentCloseTime.time_since_epoch().count();
        std::uint64_t const outCloseTimeResolution = info.closeTimeResolution.count();
        std::uint64_t const outCloseFlags = info.closeFlags;
        std::string const outAccountHash = to_string(info.accountHash);
        std::string const outTxHash = to_string(info.txHash);

        auto const& ledger = json[jss::result][jss::ledger];
        return outHash == ledger[jss::ledger_hash].asString() && outSeq == seq &&
            outParentHash == ledger[jss::parent_hash].asString() &&
            outDrops == ledger[jss::total_coins].asString() &&
            outCloseTime == ledger[jss::close_time].asUInt() &&
            outParentCloseTime == ledger[jss::parent_close_time].asUInt() &&
            outCloseTimeResolution == ledger[jss::close_time_resolution].asUInt() &&
            outCloseFlags == ledger[jss::close_flags].asUInt() &&
            outAccountHash == ledger[jss::account_hash].asString() &&
            outTxHash == ledger[jss::transaction_hash].asString();
    }

    static bool
    bad(json::Value const& json, ErrorCodeI error = RpcLgrNotFound)
    {
        return json.isMember(jss::result) && rpc::containsError(json[jss::result]) &&
            json[jss::result][jss::error_code] == error;
    }

    std::string
    getHash(json::Value const& json)
    {
        BEAST_EXPECT(
            json.isMember(jss::result) && json[jss::result].isMember(jss::ledger) &&
            json[jss::result][jss::ledger].isMember(jss::ledger_hash) &&
            json[jss::result][jss::ledger][jss::ledger_hash].isString());
        return json[jss::result][jss::ledger][jss::ledger_hash].asString();
    }

    void
    ledgerCheck(jtx::Env& env, int const rows, int const first)
    {
        auto const [actualRows, actualFirst, actualLast] =
            env.app().getRelationalDatabase().getLedgerCountMinMax();

        BEAST_EXPECT(actualRows == rows);
        BEAST_EXPECT(actualFirst == first);
        BEAST_EXPECT(actualLast == first + rows - 1);
    }

    void
    transactionCheck(jtx::Env& env, int const rows)
    {
        BEAST_EXPECT(env.app().getRelationalDatabase().getTransactionCount() == rows);
    }

    void
    accountTransactionCheck(jtx::Env& env, int const rows)
    {
        BEAST_EXPECT(env.app().getRelationalDatabase().getAccountTransactionCount() == rows);
    }

    int
    waitForReady(jtx::Env& env)
    {
        using namespace std::chrono_literals;

        auto& store = env.app().getSHAMapStore();

        int ledgerSeq = 3;
        store.rendezvous();
        BEAST_EXPECT(!store.getLastRotated());

        env.close();
        store.rendezvous();

        auto ledger = env.rpc("ledger", "validated");
        BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq++)));

        BEAST_EXPECT(store.getLastRotated() == ledgerSeq - 1);
        return ledgerSeq;
    }

public:
    void
    testClear()
    {
        using namespace std::chrono_literals;

        testcase("clearPrior");
        using namespace jtx;

        Env env(*this, envconfig(onlineDelete));

        auto& store = env.app().getSHAMapStore();
        env.fund(XRP(10000), noripple("alice"));

        ledgerCheck(env, 1, 2);
        transactionCheck(env, 0);
        accountTransactionCheck(env, 0);

        std::map<std::uint32_t, json::Value const> ledgers;

        auto ledgerTmp = env.rpc("ledger", "0");
        BEAST_EXPECT(bad(ledgerTmp));

        ledgers.emplace(1, env.rpc("ledger", "1"));
        BEAST_EXPECT(goodLedger(env, ledgers[1], "1"));

        ledgers.emplace(2, env.rpc("ledger", "2"));
        BEAST_EXPECT(goodLedger(env, ledgers[2], "2"));

        ledgerTmp = env.rpc("ledger", "current");
        BEAST_EXPECT(goodLedger(env, ledgerTmp, "3"));

        ledgerTmp = env.rpc("ledger", "4");
        BEAST_EXPECT(bad(ledgerTmp));

        ledgerTmp = env.rpc("ledger", "100");
        BEAST_EXPECT(bad(ledgerTmp));

        auto const firstSeq = waitForReady(env);
        auto lastRotated = firstSeq - 1;

        for (auto i = firstSeq + 1; i < kDeleteInterval + firstSeq; ++i)
        {
            env.fund(XRP(10000), noripple("test" + std::to_string(i)));
            env.close();

            ledgerTmp = env.rpc("ledger", "current");
            BEAST_EXPECT(goodLedger(env, ledgerTmp, std::to_string(i)));
        }
        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        for (auto i = 3; i < kDeleteInterval + lastRotated; ++i)
        {
            ledgers.emplace(i, env.rpc("ledger", std::to_string(i)));
            BEAST_EXPECT(
                goodLedger(env, ledgers[i], std::to_string(i), true) &&
                !getHash(ledgers[i]).empty());
        }

        ledgerCheck(env, kDeleteInterval + 1, 2);
        transactionCheck(env, kDeleteInterval);
        accountTransactionCheck(env, 2 * kDeleteInterval);

        {
            // Closing one more ledger triggers a rotate
            env.close();

            auto ledger = env.rpc("ledger", "current");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(kDeleteInterval + 4)));
        }

        store.rendezvous();

        BEAST_EXPECT(store.getLastRotated() == kDeleteInterval + 3);
        lastRotated = store.getLastRotated();
        BEAST_EXPECT(lastRotated == 11);

        // That took care of the fake hashes
        ledgerCheck(env, kDeleteInterval + 1, 3);
        transactionCheck(env, kDeleteInterval);
        accountTransactionCheck(env, 2 * kDeleteInterval);

        // The last iteration of this loop should trigger a rotate
        for (auto i = lastRotated - 1; i < lastRotated + kDeleteInterval - 1; ++i)
        {
            env.close();

            ledgerTmp = env.rpc("ledger", "current");
            BEAST_EXPECT(goodLedger(env, ledgerTmp, std::to_string(i + 3)));

            ledgers.emplace(i, env.rpc("ledger", std::to_string(i)));
            BEAST_EXPECT(
                store.getLastRotated() == lastRotated || i == lastRotated + kDeleteInterval - 2);
            BEAST_EXPECT(
                goodLedger(env, ledgers[i], std::to_string(i), true) &&
                !getHash(ledgers[i]).empty());
        }

        store.rendezvous();

        BEAST_EXPECT(store.getLastRotated() == kDeleteInterval + lastRotated);

        ledgerCheck(env, kDeleteInterval + 1, lastRotated);
        transactionCheck(env, 0);
        accountTransactionCheck(env, 0);
    }

    void
    testAutomatic()
    {
        testcase("automatic online_delete");
        using namespace jtx;
        using namespace std::chrono_literals;

        Env env(*this, envconfig(onlineDelete));
        auto& store = env.app().getSHAMapStore();

        auto ledgerSeq = waitForReady(env);
        auto lastRotated = ledgerSeq - 1;
        BEAST_EXPECT(store.getLastRotated() == lastRotated);
        BEAST_EXPECT(lastRotated != 2);

        // Because advisory_delete is unset,
        // "can_delete" is disabled.
        auto const canDelete = env.rpc("can_delete");
        BEAST_EXPECT(bad(canDelete, RpcNotEnabled));

        // Close ledgers without triggering a rotate
        for (; ledgerSeq < lastRotated + kDeleteInterval; ++ledgerSeq)
        {
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        store.rendezvous();

        // The database will always have back to ledger 2,
        // regardless of lastRotated.
        ledgerCheck(env, ledgerSeq - 2, 2);
        BEAST_EXPECT(lastRotated == store.getLastRotated());

        {
            // Closing one more ledger triggers a rotate
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        store.rendezvous();

        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);
        BEAST_EXPECT(lastRotated != store.getLastRotated());

        lastRotated = store.getLastRotated();

        // Close enough ledgers to trigger another rotate
        for (; ledgerSeq < lastRotated + kDeleteInterval + 1; ++ledgerSeq)
        {
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        store.rendezvous();

        ledgerCheck(env, kDeleteInterval + 1, lastRotated);
        BEAST_EXPECT(lastRotated != store.getLastRotated());
    }

    void
    testCanDelete()
    {
        testcase("online_delete with advisory_delete");
        using namespace jtx;
        using namespace std::chrono_literals;

        // Same config with advisory_delete enabled
        Env env(*this, envconfig(advisoryDelete));
        auto& store = env.app().getSHAMapStore();

        auto ledgerSeq = waitForReady(env);
        auto lastRotated = ledgerSeq - 1;
        BEAST_EXPECT(store.getLastRotated() == lastRotated);
        BEAST_EXPECT(lastRotated != 2);

        auto canDelete = env.rpc("can_delete");
        BEAST_EXPECT(!rpc::containsError(canDelete[jss::result]));
        BEAST_EXPECT(canDelete[jss::result][jss::can_delete] == 0);

        canDelete = env.rpc("can_delete", "never");
        BEAST_EXPECT(!rpc::containsError(canDelete[jss::result]));
        BEAST_EXPECT(canDelete[jss::result][jss::can_delete] == 0);

        auto const firstBatch = kDeleteInterval + ledgerSeq;
        for (; ledgerSeq < firstBatch; ++ledgerSeq)
        {
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        store.rendezvous();

        ledgerCheck(env, ledgerSeq - 2, 2);
        BEAST_EXPECT(lastRotated == store.getLastRotated());

        // This does not kick off a cleanup
        canDelete = env.rpc("can_delete", std::to_string(ledgerSeq + (kDeleteInterval / 2)));
        BEAST_EXPECT(!rpc::containsError(canDelete[jss::result]));
        BEAST_EXPECT(canDelete[jss::result][jss::can_delete] == ledgerSeq + (kDeleteInterval / 2));

        store.rendezvous();

        ledgerCheck(env, ledgerSeq - 2, 2);
        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off a cleanup, but it stays small.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        store.rendezvous();

        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);

        BEAST_EXPECT(store.getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;

        for (; ledgerSeq < lastRotated + kDeleteInterval; ++ledgerSeq)
        {
            // No cleanups in this loop.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        store.rendezvous();

        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off another cleanup.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        store.rendezvous();

        ledgerCheck(env, ledgerSeq - firstBatch, firstBatch);

        BEAST_EXPECT(store.getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;

        // This does not kick off a cleanup
        canDelete = env.rpc("can_delete", "always");
        BEAST_EXPECT(!rpc::containsError(canDelete[jss::result]));
        BEAST_EXPECT(
            canDelete[jss::result][jss::can_delete] == std::numeric_limits<unsigned int>::max());

        for (; ledgerSeq < lastRotated + kDeleteInterval; ++ledgerSeq)
        {
            // No cleanups in this loop.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        store.rendezvous();

        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off another cleanup.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        store.rendezvous();

        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);

        BEAST_EXPECT(store.getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;

        // This does not kick off a cleanup
        canDelete = env.rpc("can_delete", "now");
        BEAST_EXPECT(!rpc::containsError(canDelete[jss::result]));
        BEAST_EXPECT(canDelete[jss::result][jss::can_delete] == ledgerSeq - 1);

        for (; ledgerSeq < lastRotated + kDeleteInterval; ++ledgerSeq)
        {
            // No cleanups in this loop.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        store.rendezvous();

        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off another cleanup.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        store.rendezvous();

        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);

        BEAST_EXPECT(store.getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;
    }

    std::unique_ptr<node_store::Backend>
    makeBackendRotating(jtx::Env& env, NodeStoreScheduler& scheduler, std::string path)
    {
        Section section{env.app().config().section(Sections::kNodeDatabase)};
        boost::filesystem::path newPath;

        if (!BEAST_EXPECT(path.size()))
            return {};
        newPath = path;
        section.set(Keys::kPath, newPath.string());

        auto backend{node_store::Manager::instance().makeBackend(
            section,
            megabytes(env.app().config().getValueFor(SizedItem::BurstSize, std::nullopt)),
            scheduler,
            env.app().getJournal("NodeStoreTest"))};
        backend->open();
        return backend;
    }

    void
    testRotate()
    {
        // The only purpose of this test is to ensure that if something that
        // should never happen happens, we don't get a deadlock.
        testcase("rotate with lock contention");

        using namespace jtx;
        Env env(*this, envconfig(onlineDelete));

        /////////////////////////////////////////////////////////////
        // Create NodeStore with two backends to allow online deletion of data.
        // Normally, SHAMapStoreImp handles all these details.
        auto nscfg = env.app().config().section(Sections::kNodeDatabase);

        // Provide default values.
        if (!nscfg.exists(Keys::kCacheSize))
        {
            nscfg.set(
                Keys::kCacheSize,
                std::to_string(
                    env.app().config().getValueFor(SizedItem::TreeCacheSize, std::nullopt)));
        }

        if (!nscfg.exists(Keys::kCacheAge))
        {
            nscfg.set(
                Keys::kCacheAge,
                std::to_string(
                    env.app().config().getValueFor(SizedItem::TreeCacheAge, std::nullopt)));
        }

        NodeStoreScheduler scheduler(env.app().getJobQueue());

        // Open a two-generation ring, oldest -> newest: {"archive", "write"}.
        std::vector<std::shared_ptr<node_store::Backend>> generations;
        generations.emplace_back(makeBackendRotating(env, scheduler, "archive"));
        generations.emplace_back(makeBackendRotating(env, scheduler, "write"));

        static constexpr int kReadThreads = 4;
        auto dbr = std::make_unique<node_store::DatabaseRotatingImp>(
            scheduler,
            kReadThreads,
            std::move(generations),
            nscfg,
            env.app().getJournal("NodeStoreTest"));

        /////////////////////////////////////////////////////////////
        // Check basic functionality
        using namespace std::chrono_literals;
        std::atomic<int> threadNum = 0;

        BEAST_EXPECT(dbr->getName() == "write");
        BEAST_EXPECT(dbr->generationCount() == 2);

        // advance: append a fresh writable generation "1". The prior writable stays in
        // the ring; the persist callback receives the whole ring, oldest -> newest.
        {
            auto newBackend = makeBackendRotating(env, scheduler, std::to_string(++threadNum));
            std::vector<std::string> persisted;
            dbr->advance(std::move(newBackend), [&](std::vector<std::string> const& generations) {
                persisted = generations;
                // Ensure that dbr functions can be called from within the callback
                BEAST_EXPECT(dbr->getName() == "1");
            });
            BEAST_EXPECT((persisted == std::vector<std::string>{"archive", "write", "1"}));
        }
        BEAST_EXPECT(threadNum == 1);
        BEAST_EXPECT(dbr->getName() == "1");
        BEAST_EXPECT(dbr->generationCount() == 3);

        // retire the oldest generation ("archive"); the ring shrinks and the persist
        // callback receives the shortened ring.
        {
            dbr->beginRetire();
            std::vector<std::string> persisted;
            dbr->retireOldest([&](std::vector<std::string> const& generations) {
                persisted = generations;
                BEAST_EXPECT(dbr->getName() == "1");
            });
            dbr->endRetire();
            BEAST_EXPECT((persisted == std::vector<std::string>{"write", "1"}));
        }
        BEAST_EXPECT(dbr->getName() == "1");
        BEAST_EXPECT(dbr->generationCount() == 2);

        // retireOldest never drops the sole writable generation.
        {
            dbr->retireOldest([&](std::vector<std::string> const& generations) {
                BEAST_EXPECT((generations == std::vector<std::string>{"1"}));
            });
            BEAST_EXPECT(dbr->generationCount() == 1);

            bool retiredWritable = false;
            dbr->retireOldest([&](std::vector<std::string> const&) { retiredWritable = true; });
            BEAST_EXPECT(!retiredWritable);
            BEAST_EXPECT(dbr->generationCount() == 1);
            BEAST_EXPECT(dbr->getName() == "1");
        }

        /////////////////////////////////////////////////////////////
        // Do something stupid. Re-enter advance from inside the persist callback.
        {
            auto const cbInner = [&](std::vector<std::string> const& generations) {
                BEAST_EXPECT((generations == std::vector<std::string>{"1", "2", "3"}));
                BEAST_EXPECT(dbr->getName() == "3");
            };
            auto const cbReentrant = [&](std::vector<std::string> const& generations) {
                BEAST_EXPECT((generations == std::vector<std::string>{"1", "2"}));
                auto newBackend = makeBackendRotating(env, scheduler, std::to_string(++threadNum));
                // Reminder: doing this is stupid and should never happen
                dbr->advance(std::move(newBackend), cbInner);
            };
            auto newBackend = makeBackendRotating(env, scheduler, std::to_string(++threadNum));
            dbr->advance(std::move(newBackend), cbReentrant);
        }
        BEAST_EXPECT(threadNum == 3);
        BEAST_EXPECT(dbr->getName() == "3");
        BEAST_EXPECT(dbr->generationCount() == 3);

        // Equally stupid: re-enter retireOldest from inside its persist callback.
        {
            bool innerRan = false;
            dbr->beginRetire();
            dbr->retireOldest([&](std::vector<std::string> const& generations) {
                BEAST_EXPECT((generations == std::vector<std::string>{"2", "3"}));
                dbr->retireOldest([&](std::vector<std::string> const& inner) {
                    BEAST_EXPECT((inner == std::vector<std::string>{"3"}));
                    innerRan = true;
                });
            });
            dbr->endRetire();
            BEAST_EXPECT(innerRan);
            BEAST_EXPECT(dbr->generationCount() == 1);
            BEAST_EXPECT(dbr->getName() == "3");
        }
    }

    // Store a node into the writable generation and return its hash. A distinct tag byte
    // gives each node a distinct key and payload.
    static uint256
    storeNode(node_store::DatabaseRotating& dbr, std::uint8_t tag)
    {
        uint256 hash;
        hash.begin()[0] = tag;
        Blob blob{tag, tag, tag};
        dbr.store(NodeObjectType::AccountNode, std::move(blob), hash, 1);
        return hash;
    }

    static bool
    hasNode(node_store::DatabaseRotating& dbr, uint256 const& hash)
    {
        return dbr.fetchNodeObject(hash, 0, node_store::FetchType::Synchronous, false) != nullptr;
    }

    void
    testRetention()
    {
        // Prove the generational invariant the whole feature rests on: retiring the oldest
        // generation preserves its still-live nodes (evacuated forward) and reclaims only
        // its dead ones, and evacuation is scoped to the retiring generation so nodes in
        // other sealed generations are never needlessly copied. This is also the recovery
        // guarantee — a node retained across a rotation is still fetchable afterwards.
        testcase("generational retention and evacuation");

        using namespace jtx;
        Env env(*this, envconfig(onlineDelete));
        NodeStoreScheduler scheduler(env.app().getJobQueue());
        auto nscfg = env.app().config().section(Sections::kNodeDatabase);

        auto const noop = [](std::vector<std::string> const&) {};

        // Start with one generation (g0, writable) and grow the ring to three:
        // g0 (oldest) -> g1 (middle) -> g2 (writable).
        std::vector<std::shared_ptr<node_store::Backend>> generations;
        generations.emplace_back(makeBackendRotating(env, scheduler, "g0"));
        auto dbr = std::make_unique<node_store::DatabaseRotatingImp>(
            scheduler, 4, std::move(generations), nscfg, env.app().getJournal("NodeStoreTest"));

        // Into g0: a live node (X, will be evacuated) and a dead node (Z, never touched
        // during the retire window, so it must be reclaimed with the generation).
        auto const x = storeNode(*dbr, 0x11);
        auto const z = storeNode(*dbr, 0x22);

        dbr->advance(makeBackendRotating(env, scheduler, "g1"), noop);
        // Into g1: a live node (Y) in a generation that will NOT be retired.
        auto const y = storeNode(*dbr, 0x33);

        dbr->advance(makeBackendRotating(env, scheduler, "g2"), noop);
        BEAST_EXPECT(dbr->generationCount() == 3);
        BEAST_EXPECT(dbr->getName() == "g2");

        // Retire the oldest generation (g0). During the window, evacuate live nodes by
        // fetching them — exactly what SHAMapStore does via visitNodes(copyNode).
        dbr->beginRetire();

        // X is served by the retiring generation, so it is copied forward into the
        // writable backend.
        BEAST_EXPECT(hasNode(*dbr, x));
        BEAST_EXPECT(dbr->copyForwardCount() == 1);

        // Y is served by a sealed but non-retiring generation: found, but NOT copied — the
        // property that keeps evacuation O(churn) rather than O(total state).
        BEAST_EXPECT(hasNode(*dbr, y));
        BEAST_EXPECT(dbr->copyForwardCount() == 1);

        // Fetching X again now hits the writable copy first, so it is not copied twice.
        BEAST_EXPECT(hasNode(*dbr, x));
        BEAST_EXPECT(dbr->copyForwardCount() == 1);

        dbr->endRetire();
        dbr->retireOldest(noop);
        BEAST_EXPECT(dbr->generationCount() == 2);

        // X lived only in g0; its survival proves it was evacuated to the writable backend.
        BEAST_EXPECT(hasNode(*dbr, x));
        // Z lived only in g0 and was never evacuated: reclaimed with the dropped generation.
        BEAST_EXPECT(!hasNode(*dbr, z));
        // Y lives in g1, which was not dropped: it survives without ever being copied.
        BEAST_EXPECT(hasNode(*dbr, y));

        // The rescue mechanism copyNode relies on: re-storing a lost node's in-memory
        // body into the writable generation makes it fetchable again.
        {
            Blob blob{0x22, 0x22, 0x22};
            dbr->store(NodeObjectType::AccountNode, std::move(blob), z, 0);
            BEAST_EXPECT(hasNode(*dbr, z));
        }

        // Single-generation edge: with only the writable left, a retire window copies
        // nothing forward (a writable hit is not an evacuation) and retireOldest
        // refuses to drop the sole generation.
        {
            dbr->retireOldest(noop);
            BEAST_EXPECT(dbr->generationCount() == 1);

            dbr->beginRetire();
            auto const w = storeNode(*dbr, 0x44);
            BEAST_EXPECT(hasNode(*dbr, w));
            BEAST_EXPECT(dbr->copyForwardCount() == 0);
            dbr->retireOldest(noop);
            BEAST_EXPECT(dbr->generationCount() == 1);
            BEAST_EXPECT(hasNode(*dbr, w));
            dbr->endRetire();
        }
    }

    void
    testConcurrentAccess()
    {
        // Race readers against the ring lifecycle: fetches snapshot the ring lock-free
        // while the maintenance path advances, opens retire windows, and drops
        // generations. Every node fetched during each retire window must survive.
        testcase("concurrent fetch during advance and retire");

        using namespace jtx;
        Env env(*this, envconfig(onlineDelete));
        NodeStoreScheduler scheduler(env.app().getJobQueue());
        auto nscfg = env.app().config().section(Sections::kNodeDatabase);
        auto const noop = [](std::vector<std::string> const&) {};

        std::vector<std::shared_ptr<node_store::Backend>> generations;
        generations.emplace_back(makeBackendRotating(env, scheduler, "c0"));
        auto dbr = std::make_unique<node_store::DatabaseRotatingImp>(
            scheduler, 4, std::move(generations), nscfg, env.app().getJournal("NodeStoreTest"));

        std::vector<uint256> hashes;
        for (std::uint8_t tag = 1; tag <= 16; ++tag)
            hashes.push_back(storeNode(*dbr, tag));

        std::atomic<bool> done{false};
        std::vector<std::thread> readers;
        readers.reserve(4);
        for (int t = 0; t < 4; ++t)
        {
            readers.emplace_back([&] {
                while (!done.load(std::memory_order_relaxed))
                {
                    for (auto const& h : hashes)
                        hasNode(*dbr, h);
                }
            });
        }
        // A writer racing advance/retire: store() snapshots the writable outside the
        // ring lock, the exact window the lifecycle mutates.
        readers.emplace_back([&] {
            std::uint8_t tag = 0;
            while (!done.load(std::memory_order_relaxed))
                storeNode(*dbr, 100 + (tag++ % 100));
        });

        for (int cycle = 1; cycle <= 20; ++cycle)
        {
            dbr->advance(makeBackendRotating(env, scheduler, "c" + std::to_string(cycle)), noop);
            dbr->beginRetire();
            // Evacuate by fetching, as SHAMapStore does.
            for (auto const& h : hashes)
                hasNode(*dbr, h);
            dbr->retireOldest(noop);
            dbr->endRetire();
        }
        done = true;
        for (auto& r : readers)
            r.join();

        BEAST_EXPECT(dbr->generationCount() == 1);
        for (auto const& h : hashes)
            BEAST_EXPECT(hasNode(*dbr, h));
    }

    void
    testStateMigration()
    {
        // The saved-state ring round-trips, reconstructs from a legacy two-backend
        // state, and detects rows left stale by a downgraded build's rotation.
        testcase("saved state ring migration");

        using namespace jtx;
        Env env(*this, envconfig(onlineDelete));

        soci::session session;
        initStateDB(session, env.app().config(), "state_migration_test");

        // Legacy pair only (pre-ring build): the ring is reconstructed oldest -> newest.
        session << "UPDATE DbState SET WritableDb = 'write', ArchiveDb = 'archive',"
                   " LastRotatedLedger = 42 WHERE Key = 1;";
        auto state = getSavedState(session);
        BEAST_EXPECT((state.generations == std::vector<std::string>{"archive", "write"}));
        BEAST_EXPECT(state.writableDb == "write");
        BEAST_EXPECT(state.archiveDb == "archive");
        BEAST_EXPECT(state.lastRotated == 42);

        // A ring round-trips, with the legacy pair mirroring the ring ends.
        SavedState ring;
        ring.generations = {"g0", "g1", "g2"};
        ring.archiveDb = "g0";
        ring.writableDb = "g2";
        ring.lastRotated = 43;
        setSavedState(session, ring);
        state = getSavedState(session);
        BEAST_EXPECT((state.generations == std::vector<std::string>{"g0", "g1", "g2"}));
        BEAST_EXPECT(state.archiveDb == "g0");
        BEAST_EXPECT(state.writableDb == "g2");
        BEAST_EXPECT(state.lastRotated == 43);

        // Downgrade simulation: an old build's rotation rewrites the pair but leaves
        // DbGenerations untouched. The stale ring is discarded in favor of the pair.
        session << "UPDATE DbState SET WritableDb = 'new', ArchiveDb = 'g2' WHERE Key = 1;";
        state = getSavedState(session);
        BEAST_EXPECT((state.generations == std::vector<std::string>{"g2", "new"}));
        BEAST_EXPECT(state.archiveDb == "g2");
        BEAST_EXPECT(state.writableDb == "new");
    }

    void
    testGenerationalHistory()
    {
        // End-to-end retirement through the SHAMapStore run loop with the smallest
        // generation budget (2), with churn every ledger so superseded node versions
        // exist. After several retirements, the full state of the oldest retained
        // ledgers must still resolve — the retained-history guarantee.
        testcase("retirement preserves retained history");

        using namespace jtx;
        Env env(*this, envconfig(generationalDelete));
        auto& store = env.app().getSHAMapStore();

        Account const alice{"alice"};
        env.fund(XRP(10000), noripple(alice));

        auto ledgerSeq = waitForReady(env);
        LedgerIndex prevRotated = 0;

        for (int cycle = 0; cycle < 3; ++cycle)
        {
            prevRotated = store.getLastRotated();
            auto const target = prevRotated + kDeleteInterval;
            while (ledgerSeq <= static_cast<int>(target))
            {
                env(pay(env.master, alice, XRP(1)));
                env.close();

                auto const ledger = env.rpc("ledger", "validated");
                BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq++)));
                store.rendezvous();
            }
            BEAST_EXPECT(store.getLastRotated() == target);
        }

        // prevRotated is now the oldest retained ledger; versions its state references
        // that were superseded during the last interval sat in the generation retired
        // at the final rotation and survive only via boundary-root evacuation.
        for (auto const seq : {prevRotated, store.getLastRotated()})
        {
            json::Value params;
            params[jss::ledger_index] = seq;
            params[jss::limit] = 4096;
            auto const res = env.rpc("json", "ledger_data", params.toStyledString());
            BEAST_EXPECT(res.isMember(jss::result) && !rpc::containsError(res[jss::result]));
            BEAST_EXPECT(res[jss::result][jss::state].size() > 0);
            // No marker: the whole state tree resolved in one page.
            BEAST_EXPECT(!res[jss::result].isMember(jss::marker));
        }
    }

    void
    testTwoRootEvacuation()
    {
        // Deterministic proof of boundary-root evacuation. Emptying the TreeNodeCache
        // right before each rotation-triggering close leaves cache freshening nothing
        // to rescue, so the walk of the oldest retained ledger's root is the ONLY
        // mechanism that can preserve node versions that ledger references but the
        // current state no longer does (they live in the generation being retired).
        testcase("boundary-root evacuation preserves superseded versions");

        using namespace jtx;
        Env env(*this, envconfig(generationalDelete));
        auto& store = env.app().getSHAMapStore();

        Account const alice{"alice"};
        env.fund(XRP(10000), noripple(alice));

        auto ledgerSeq = waitForReady(env);
        LedgerIndex prevRotated = 0;

        for (int cycle = 0; cycle < 2; ++cycle)
        {
            prevRotated = store.getLastRotated();
            auto const target = prevRotated + kDeleteInterval;
            while (ledgerSeq <= static_cast<int>(target))
            {
                // Churn every ledger: alice's account root (and the inner nodes above
                // it) is superseded on every close.
                env(pay(env.master, alice, XRP(1)));
                if (ledgerSeq == static_cast<int>(target))
                    env.app().getNodeFamily().getTreeNodeCache()->clear();
                env.close();

                auto const ledger = env.rpc("ledger", "validated");
                BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq++)));
                store.rendezvous();
            }
            BEAST_EXPECT(store.getLastRotated() == target);
        }

        // The boundary ledger's state references versions written before the previous
        // rotation and superseded since -- exactly the contents of the generation just
        // retired. A complete walk proves they were evacuated via the boundary root.
        json::Value params;
        params[jss::ledger_index] = prevRotated;
        params[jss::limit] = 4096;
        auto const res = env.rpc("json", "ledger_data", params.toStyledString());
        BEAST_EXPECT(res.isMember(jss::result) && !rpc::containsError(res[jss::result]));
        BEAST_EXPECT(res[jss::result][jss::state].size() > 0);
        BEAST_EXPECT(!res[jss::result].isMember(jss::marker));
    }

    void
    testRingConvergence()
    {
        // A ring persisted over budget -- rotations interrupted between advance and
        // retire, or a lowered online_delete_generations -- must converge back to
        // budget on the next rotation instead of staying inflated forever. Phase 1
        // grows a 4-generation ring under a budget of 4 (no retirement) on real disk
        // backends; phase 2 reboots the same data with a budget of 2 and expects the
        // first rotation to retire all the way back down.
        testcase("ring converges to budget after over-budget boot");

        using namespace jtx;
        namespace bfs = boost::filesystem;

        auto const root = bfs::temp_directory_path() / bfs::unique_path("shamapstore_conv_%%%%");
        auto const nodeDb = (root / "nudb").string();
        auto const stateDir = (root / "state").string();
        bfs::create_directories(nodeDb);
        bfs::create_directories(stateDir);

        {
            Env env(*this, diskConfig(nodeDb, stateDir, "4"));
            auto ledgerSeq = waitForReady(env);

            // Two rotations grow the ring 2 -> 3 -> 4, never exceeding the budget,
            // so no generation is ever retired.
            rotateOnce(env, ledgerSeq);
            rotateOnce(env, ledgerSeq);

            auto const& dbr = dynamic_cast<node_store::DatabaseRotating&>(env.app().getNodeStore());
            BEAST_EXPECT(dbr.generationCount() == 4);
        }

        {
            Env env(*this, diskConfig(nodeDb, stateDir, "2"));
            auto& store = env.app().getSHAMapStore();
            auto const& dbr = dynamic_cast<node_store::DatabaseRotating&>(env.app().getNodeStore());

            // The persisted 4-generation ring reopened, now over a budget of 2.
            BEAST_EXPECT(dbr.generationCount() == 4);
            auto const bootRotated = store.getLastRotated();
            BEAST_EXPECT(bootRotated != 0);

            // The fresh genesis chain must catch up to bootRotated + interval before
            // the next rotation fires.
            for (int i = 0; i < static_cast<int>(bootRotated) + (2 * kDeleteInterval) &&
                 store.getLastRotated() == bootRotated;
                 ++i)
            {
                env.close();
                store.rendezvous();
            }
            BEAST_EXPECT(store.getLastRotated() != bootRotated);

            // One rotation: advance made it 5; the retire loop must shed 3, not 1.
            BEAST_EXPECT(dbr.generationCount() == 2);
        }

        bfs::remove_all(root);
    }

    void
    testMinGenerationsClamp()
    {
        // online_delete_generations below the floor is clamped to 2, preserving the
        // writable + one archive invariant: a budget of 1 would drop the only sealed
        // generation immediately after every rotation.
        testcase("online_delete_generations clamps to the minimum");

        using namespace jtx;
        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg = onlineDelete(std::move(cfg));
            cfg->section(Sections::kNodeDatabase).set(Keys::kOnlineDeleteGenerations, "1");
            return cfg;
        }));
        auto const& dbr = dynamic_cast<node_store::DatabaseRotating&>(env.app().getNodeStore());

        auto ledgerSeq = waitForReady(env);
        rotateOnce(env, ledgerSeq);

        // Clamped to 2: the rotation retired down to two generations, not one.
        BEAST_EXPECT(dbr.generationCount() == 2);
    }

    void
    testCorruptedStateRefusal()
    {
        // A generation named in the persisted ring but missing on disk means the data
        // is unusable; the server must refuse to start rather than silently open a
        // ring with a hole in it.
        testcase("boot refuses a ring with a missing generation");

        using namespace jtx;
        namespace bfs = boost::filesystem;

        auto const root = bfs::temp_directory_path() / bfs::unique_path("shamapstore_miss_%%%%");
        auto const nodeDb = (root / "nudb").string();
        auto const stateDir = (root / "state").string();
        bfs::create_directories(nodeDb);
        bfs::create_directories(stateDir);

        {
            Env env(*this, diskConfig(nodeDb, stateDir, "4"));
            auto ledgerSeq = waitForReady(env);
            rotateOnce(env, ledgerSeq);
            auto const& dbr = dynamic_cast<node_store::DatabaseRotating&>(env.app().getNodeStore());
            BEAST_EXPECT(dbr.generationCount() == 3);
        }

        std::vector<bfs::path> generations;
        for (bfs::directory_iterator it(nodeDb); it != bfs::directory_iterator(); ++it)
            generations.push_back(it->path());
        BEAST_EXPECT(generations.size() == 3);
        bfs::remove_all(generations.front());

        bool threw = false;
        try
        {
            Env const env(*this, diskConfig(nodeDb, stateDir, "4"));
        }
        catch (std::exception const&)
        {
            threw = true;
        }
        BEAST_EXPECT(threw);

        bfs::remove_all(root);
    }

    void
    testOrphanCleanup()
    {
        // A directory with the backend prefix that is not in the persisted ring is an
        // orphan (created but never persisted before a crash) and is removed at boot;
        // unrelated directories are left alone.
        testcase("orphan generation directories are removed at boot");

        using namespace jtx;
        namespace bfs = boost::filesystem;

        auto const root = bfs::temp_directory_path() / bfs::unique_path("shamapstore_orph_%%%%");
        auto const nodeDb = (root / "nudb").string();
        auto const stateDir = (root / "state").string();
        bfs::create_directories(nodeDb);
        bfs::create_directories(stateDir);

        {
            Env env(*this, diskConfig(nodeDb, stateDir, "4"));
            auto ledgerSeq = waitForReady(env);
            rotateOnce(env, ledgerSeq);
        }

        auto const orphan = bfs::path(nodeDb) / "rippledb.orphan";  // cspell: disable-line
        auto const unrelated = bfs::path(nodeDb) / "unrelated";
        bfs::create_directories(orphan);
        bfs::create_directories(unrelated);

        {
            Env env(*this, diskConfig(nodeDb, stateDir, "4"));
            auto const& dbr = dynamic_cast<node_store::DatabaseRotating&>(env.app().getNodeStore());
            BEAST_EXPECT(dbr.generationCount() == 3);
            BEAST_EXPECT(!bfs::exists(orphan));
            BEAST_EXPECT(bfs::exists(unrelated));
        }

        bfs::remove_all(root);
    }

    void
    testPathRelocation()
    {
        // When the configured node_db path changes, every stored generation name is
        // rewritten to the new directory (keeping filenames) and the ring reopens.
        testcase("ring survives a node_db path change");

        using namespace jtx;
        namespace bfs = boost::filesystem;

        auto const root = bfs::temp_directory_path() / bfs::unique_path("shamapstore_relo_%%%%");
        auto const nodeDbA = (root / "nudb_a").string();
        auto const nodeDbB = (root / "nudb_b").string();
        auto const stateDir = (root / "state").string();
        bfs::create_directories(nodeDbA);
        bfs::create_directories(nodeDbB);
        bfs::create_directories(stateDir);

        {
            Env env(*this, diskConfig(nodeDbA, stateDir, "4"));
            auto ledgerSeq = waitForReady(env);
            rotateOnce(env, ledgerSeq);
            auto const& dbr = dynamic_cast<node_store::DatabaseRotating&>(env.app().getNodeStore());
            BEAST_EXPECT(dbr.generationCount() == 3);
        }

        // The operator moves the data and repoints the config.
        for (bfs::directory_iterator it(nodeDbA); it != bfs::directory_iterator(); ++it)
            bfs::rename(it->path(), bfs::path(nodeDbB) / it->path().filename());

        {
            Env env(*this, diskConfig(nodeDbB, stateDir, "4"));
            auto& store = env.app().getSHAMapStore();
            auto const& dbr = dynamic_cast<node_store::DatabaseRotating&>(env.app().getNodeStore());
            BEAST_EXPECT(dbr.generationCount() == 3);

            // Still operational: the relocated ring rotates normally.
            auto const bootRotated = store.getLastRotated();
            for (int i = 0; i < static_cast<int>(bootRotated) + (2 * kDeleteInterval) &&
                 store.getLastRotated() == bootRotated;
                 ++i)
            {
                env.close();
                store.rendezvous();
            }
            BEAST_EXPECT(store.getLastRotated() != bootRotated);
            BEAST_EXPECT(dbr.generationCount() == 4);
        }

        bfs::remove_all(root);
    }

    void
    run() override
    {
        testClear();
        testAutomatic();
        testCanDelete();
        testRotate();
        testRetention();
        testConcurrentAccess();
        testStateMigration();
        testGenerationalHistory();
        testTwoRootEvacuation();
        testRingConvergence();
        testMinGenerationsClamp();
        testCorruptedStateRefusal();
        testOrphanCleanup();
        testPathRelocation();
    }
};

// VFALCO This test fails because of thread asynchronous issues
BEAST_DEFINE_TESTSUITE(SHAMapStore, app, xrpl);

}  // namespace xrpl::test
