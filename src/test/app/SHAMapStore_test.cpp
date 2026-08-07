#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/noop.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/main/NodeStoreScheduler.h>
#include <xrpld/app/misc/SHAMapStore.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/json/json_value.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/detail/DatabaseRotatingImp.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <boost/filesystem/path.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

namespace xrpl::test {

class SHAMapStore_test : public beast::unit_test::Suite
{
    static auto const kDeleteInterval = 8;

    static auto
    onlineDelete(std::unique_ptr<Config> cfg)
    {
        return jtx::onlineDelete(std::move(cfg), kDeleteInterval);
    }

    static auto
    advisoryDelete(std::unique_ptr<Config> cfg)
    {
        cfg = onlineDelete(std::move(cfg));
        cfg->section(Sections::kNodeDatabase).set(Keys::kAdvisoryDelete, "1");
        return cfg;
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
        BEAST_EXPECT(store.rendezvous());
        BEAST_EXPECT(!store.getLastRotated());

        env.close();
        BEAST_EXPECT(store.rendezvous());

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

        BEAST_EXPECT(store.rendezvous());

        BEAST_EXPECT(env.closed()->header().seq == kDeleteInterval + 3);
        lastRotated = store.getLastRotated();
        BEAST_EXPECT(lastRotated == kDeleteInterval + 5);
        BEAST_EXPECT(lastRotated == 13);

        // That took care of the fake hashes
        ledgerCheck(env, kDeleteInterval + 1, 3);
        transactionCheck(env, kDeleteInterval);
        accountTransactionCheck(env, 2 * kDeleteInterval);

        // The last iteration of this loop should trigger a rotate
        for (auto i = lastRotated - 3; i < lastRotated + kDeleteInterval - 1; ++i)
        {
            BEAST_EXPECT(store.getLastRotated() == lastRotated);

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

        BEAST_EXPECT(store.rendezvous());

        BEAST_EXPECT(store.getLastRotated() == kDeleteInterval + lastRotated + 2);

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

        BEAST_EXPECT(store.rendezvous());

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

        BEAST_EXPECT(store.rendezvous());

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

        BEAST_EXPECT(store.rendezvous());

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

        BEAST_EXPECT(store.rendezvous());

        ledgerCheck(env, ledgerSeq - 2, 2);
        BEAST_EXPECT(lastRotated == store.getLastRotated());

        // This does not kick off a cleanup
        canDelete = env.rpc("can_delete", std::to_string(ledgerSeq + (kDeleteInterval / 2)));
        BEAST_EXPECT(!rpc::containsError(canDelete[jss::result]));
        BEAST_EXPECT(canDelete[jss::result][jss::can_delete] == ledgerSeq + (kDeleteInterval / 2));

        BEAST_EXPECT(store.rendezvous());

        ledgerCheck(env, ledgerSeq - 2, 2);
        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off a cleanup, but it stays small.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        BEAST_EXPECT(store.rendezvous());

        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);

        lastRotated = store.getLastRotated();
        BEAST_EXPECT(lastRotated == ledgerSeq + 1);

        for (; ledgerSeq < lastRotated + kDeleteInterval; ++ledgerSeq)
        {
            // No cleanups in this loop.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        BEAST_EXPECT(store.rendezvous());

        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off another cleanup.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        BEAST_EXPECT(store.rendezvous());

        ledgerCheck(env, ledgerSeq - firstBatch - 2, firstBatch + 2);

        lastRotated = store.getLastRotated();
        BEAST_EXPECT(lastRotated == ledgerSeq + 1);

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

        BEAST_EXPECT(store.rendezvous());

        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off another cleanup.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        BEAST_EXPECT(store.rendezvous());

        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);

        lastRotated = store.getLastRotated();
        BEAST_EXPECT(lastRotated == ledgerSeq + 1);

        // This does not kick off a cleanup
        canDelete = env.rpc("can_delete", "now");
        BEAST_EXPECT(!rpc::containsError(canDelete[jss::result]));
        BEAST_EXPECT(canDelete[jss::result][jss::can_delete] == lastRotated);

        for (; ledgerSeq < lastRotated + kDeleteInterval; ++ledgerSeq)
        {
            // No cleanups in this loop.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        BEAST_EXPECT(store.rendezvous());

        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off another cleanup.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        BEAST_EXPECT(store.rendezvous());

        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);

        lastRotated = store.getLastRotated();
        BEAST_EXPECT(lastRotated == ledgerSeq + 1);
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

        std::string const writableDb = "write";
        std::string const archiveDb = "archive";
        auto writableBackend = makeBackendRotating(env, scheduler, writableDb);
        auto archiveBackend = makeBackendRotating(env, scheduler, archiveDb);

        static constexpr int kReadThreads = 4;
        auto dbr = std::make_unique<node_store::DatabaseRotatingImp>(
            scheduler,
            kReadThreads,
            std::move(writableBackend),
            std::move(archiveBackend),
            nscfg,
            env.app().getJournal("NodeStoreTest"));

        /////////////////////////////////////////////////////////////
        // Check basic functionality
        using namespace std::chrono_literals;
        std::atomic<int> threadNum = 0;

        {
            auto newBackend = makeBackendRotating(env, scheduler, std::to_string(++threadNum));

            auto const cb = [&](std::string const& writableName, std::string const& archiveName) {
                BEAST_EXPECT(writableName == "1");
                BEAST_EXPECT(archiveName == "write");
                // Ensure that dbr functions can be called from within the
                // callback
                BEAST_EXPECT(dbr->getName() == "1");
            };

            dbr->rotate(std::move(newBackend), cb);
        }
        BEAST_EXPECT(threadNum == 1);
        BEAST_EXPECT(dbr->getName() == "1");

        /////////////////////////////////////////////////////////////
        // Do something stupid. Try to re-enter rotate from inside the callback.
        {
            auto const cb = [&](std::string const& writableName, std::string const& archiveName) {
                BEAST_EXPECT(writableName == "3");
                BEAST_EXPECT(archiveName == "2");
                // Ensure that dbr functions can be called from within the
                // callback
                BEAST_EXPECT(dbr->getName() == "3");
            };
            auto const cbReentrant = [&](std::string const& writableName,
                                         std::string const& archiveName) {
                BEAST_EXPECT(writableName == "2");
                BEAST_EXPECT(archiveName == "1");
                auto newBackend = makeBackendRotating(env, scheduler, std::to_string(++threadNum));
                // Reminder: doing this is stupid and should never happen
                dbr->rotate(std::move(newBackend), cb);
            };
            auto newBackend = makeBackendRotating(env, scheduler, std::to_string(++threadNum));
            dbr->rotate(std::move(newBackend), cbReentrant);
        }

        BEAST_EXPECT(threadNum == 3);
        BEAST_EXPECT(dbr->getName() == "3");
    }

    void
    testLedgerGaps()
    {
        // Note that this test is intentionally very similar to
        // LedgerMaster_test::testCompleteLedgerRange, but has a different
        // focus.

        testcase("Wait for ledger gaps to fill in");

        using namespace test::jtx;

        Env env{*this, envconfig(onlineDelete)};

        auto& ns = env.app().getNodeStore();
        std::map<LedgerIndex, uint256> hashes;
        auto storeHash = [&]() {
            hashes.emplace(env.current()->header().seq, env.current()->header().hash);

            auto const& root = ns.fetchNodeObject(hashes[env.current()->header().seq]);
            BEAST_EXPECT(root);
        };

        auto failureMessage = [&](char const* label, auto expected, auto actual) {
            std::stringstream ss;
            ss << label << ": Expected: " << expected << ", Got: " << actual;
            return ss.str();
        };

        auto const alice = Account("alice");
        env.fund(XRP(1000), alice);
        env.close();
        storeHash();

        auto& lm = env.app().getLedgerMaster();
        LedgerIndex minSeq = 2;
        LedgerIndex maxSeq = env.closed()->header().seq;
        auto& store = env.app().getSHAMapStore();
        LedgerIndex lastRotated = store.getLastRotated();
        while (lastRotated != 3)
        {
            BEAST_EXPECT(store.rendezvous());
            lastRotated = store.getLastRotated();
        }
        BEAST_EXPECTS(maxSeq == 3, std::to_string(maxSeq));
        BEAST_EXPECTS(lm.getCompleteLedgers() == "2-3", lm.getCompleteLedgers());
        BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq, maxSeq) == 0);
        BEAST_EXPECT(minSeq + 1 > maxSeq - 1);
        BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq - 1, maxSeq + 1) == 2);
        BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq - 2, maxSeq - 2) == 2);
        BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq + 2, maxSeq + 2) == 2);

        // Close enough ledgers to rotate a few times
        while (maxSeq < 28)
        {
            for (int t = 0; t < 3; ++t)
            {
                env(noop(alice));
            }
            env.close();
            storeHash();
            BEAST_EXPECT(store.rendezvous());

            ++maxSeq;

            if (maxSeq + 1 == lastRotated + kDeleteInterval)
            {
                using namespace std::chrono_literals;

                // The next ledger will trigger a rotation. Delete the
                // current ledger from LedgerMaster.
                std::this_thread::sleep_for(100ms);
                LedgerIndex const deleteSeq = maxSeq;
                {
                    std::size_t iterations = 30;
                    while (!lm.haveLedger(deleteSeq) && --iterations > 0)
                    {
                        std::this_thread::sleep_for(100ms);
                    }
                    // Even the slowest machines should be able to finalize deleteSeq within 10
                    // loops (400ms). If this test ever actually fails feel free to lower this
                    // cutoff. The intent of this test is to flag if the loop takes a very long
                    // time, but still allow the rest of this function to finish.
                    BEAST_EXPECTS(iterations > 20, std::to_string(iterations));
                    if (!BEAST_EXPECT(lm.haveLedger(deleteSeq)))
                        return;
                }
                lm.clearLedger(deleteSeq);

                auto expectedRange = [](auto minSeq, auto deleteSeq, auto maxSeq) {
                    std::stringstream expectedRange;
                    expectedRange << minSeq << "-" << (deleteSeq - 1);
                    if (deleteSeq + 1 == maxSeq)
                    {
                        expectedRange << "," << maxSeq;
                    }
                    else if (deleteSeq < maxSeq)
                    {
                        expectedRange << "," << (deleteSeq + 1) << "-" << maxSeq;
                    }
                    return expectedRange.str();
                };
                BEAST_EXPECTS(
                    lm.getCompleteLedgers() == expectedRange(minSeq, deleteSeq, maxSeq),
                    failureMessage(
                        "Complete ledgers",
                        expectedRange(minSeq, deleteSeq, maxSeq),
                        lm.getCompleteLedgers()));
                BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq, maxSeq) == 1);

                // Close another ledger, which will trigger a rotation, but the
                // rotation will be stuck until the missing ledger is filled in.
                env.close();
                storeHash();
                // Do not call rendezvous() here without a timeout; it will block until the missing
                // ledger is backfilled. That will not happen automatically. It's a manual step that
                // is done later in this test.
                ++maxSeq;

                // Nothing has changed
                BEAST_EXPECTS(
                    store.getLastRotated() == lastRotated,
                    failureMessage("lastRotated", lastRotated, store.getLastRotated()));
                BEAST_EXPECTS(
                    lm.getCompleteLedgers() == expectedRange(minSeq, deleteSeq, maxSeq),
                    failureMessage(
                        "Complete ledgers",
                        expectedRange(minSeq, deleteSeq, maxSeq),
                        lm.getCompleteLedgers()));

                // Close 5 more ledgers, waiting a little bit in between to
                // simulate the ledger making progress while online delete waits
                // for the missing ledger to be filled in.
                // This ensures the healthWait check has time to run and
                // detect the gap.
                for (int l = 0; l < 5; ++l)
                {
                    env.close();
                    storeHash();
                    ++maxSeq;
                    // Nothing has changed
                    BEAST_EXPECTS(
                        store.getLastRotated() == lastRotated,
                        failureMessage("lastRotated", lastRotated, store.getLastRotated()));
                    BEAST_EXPECTS(
                        lm.getCompleteLedgers() == expectedRange(minSeq, deleteSeq, maxSeq),
                        failureMessage(
                            "Complete Ledgers",
                            expectedRange(minSeq, deleteSeq, maxSeq),
                            lm.getCompleteLedgers()));
                    // The Store is "stuck" in healthWait() and won't finish the run() loop until
                    // it's backfilled
                    BEAST_EXPECT(!store.rendezvous(100ms));
                }

                // Put the missing ledger back in LedgerMaster
                lm.setLedgerRangePresent(deleteSeq, deleteSeq);

                // Wait for the rotation to finish
                BEAST_EXPECT(store.rendezvous());

                minSeq = lastRotated;
                lastRotated = maxSeq + 2;

                // Bypass caching and try to load the ledger roots from the node
                // store
                for (auto const& [seq, hash] : hashes)
                {
                    auto const nodeObject = ns.fetchNodeObject(hash);
                    std::stringstream ss;
                    ss << "minSeq: " << minSeq << ", maxSeq: " << maxSeq << ", search: " << seq
                       << ". Should " << (seq < minSeq ? "NOT " : "") << "be found";
                    if (seq < minSeq)
                        BEAST_EXPECTS(!nodeObject, ss.str());
                    else
                        BEAST_EXPECTS(nodeObject, ss.str());
                }
            }
            BEAST_EXPECT(maxSeq != lastRotated + kDeleteInterval);
            BEAST_EXPECTS(
                env.closed()->header().seq == maxSeq,
                failureMessage("maxSeq", maxSeq, env.closed()->header().seq));
            BEAST_EXPECTS(
                store.getLastRotated() == lastRotated,
                failureMessage("lastRotated", lastRotated, store.getLastRotated()));
            std::stringstream expectedRange;
            expectedRange << minSeq << "-" << maxSeq;
            BEAST_EXPECTS(
                lm.getCompleteLedgers() == expectedRange.str(),
                failureMessage("CompleteLedgers", expectedRange.str(), lm.getCompleteLedgers()));
            BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq, maxSeq) == 0);
            BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq + 1, maxSeq - 1) == 0);
            BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq - 1, maxSeq + 1) == 2);
            BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq - 2, maxSeq - 2) == 2);
            BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq + 2, maxSeq + 2) == 2);
        }
    }

    void
    run() override
    {
        testClear();
        testAutomatic();
        testCanDelete();
        testRotate();
        testLedgerGaps();
    }
};

// VFALCO This test fails because of thread asynchronous issues
BEAST_DEFINE_TESTSUITE(SHAMapStore, app, xrpl);

}  // namespace xrpl::test
