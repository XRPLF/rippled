//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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
#include <test/jtx/envconfig.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/main/NodeStoreScheduler.h>
#include <xrpld/app/misc/SHAMapStore.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/core/ConfigSections.h>
#include <xrpld/nodestore/detail/DatabaseRotatingImp.h>

#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

class SHAMapStore_test : public beast::unit_test::suite
{
    static auto const deleteInterval = 8;

    static auto
    onlineDelete(std::unique_ptr<Config> cfg)
    {
        using namespace jtx;
        return online_delete(std::move(cfg), deleteInterval);
    }

    static auto
    advisoryDelete(std::unique_ptr<Config> cfg)
    {
        cfg = onlineDelete(std::move(cfg));
        cfg->section(ConfigSection::nodeDatabase()).set("advisory_delete", "1");
        return cfg;
    }

    bool
    goodLedger(
        jtx::Env& env,
        Json::Value const& json,
        std::string ledgerID,
        bool checkDB = false)
    {
        auto good = json.isMember(jss::result) &&
            !RPC::contains_error(json[jss::result]) &&
            json[jss::result][jss::ledger][jss::ledger_index] == ledgerID;
        if (!good || !checkDB)
            return good;

        auto const seq = json[jss::result][jss::ledger_index].asUInt();

        std::optional<LedgerInfo> oinfo =
            env.app().getRelationalDatabase().getLedgerInfoByIndex(seq);
        if (!oinfo)
            return false;
        LedgerInfo const& info = oinfo.value();

        std::string const outHash = to_string(info.hash);
        LedgerIndex const outSeq = info.seq;
        std::string const outParentHash = to_string(info.parentHash);
        std::string const outDrops = to_string(info.drops);
        std::uint64_t const outCloseTime =
            info.closeTime.time_since_epoch().count();
        std::uint64_t const outParentCloseTime =
            info.parentCloseTime.time_since_epoch().count();
        std::uint64_t const outCloseTimeResolution =
            info.closeTimeResolution.count();
        std::uint64_t const outCloseFlags = info.closeFlags;
        std::string const outAccountHash = to_string(info.accountHash);
        std::string const outTxHash = to_string(info.txHash);

        auto const& ledger = json[jss::result][jss::ledger];
        return outHash == ledger[jss::ledger_hash].asString() &&
            outSeq == seq &&
            outParentHash == ledger[jss::parent_hash].asString() &&
            outDrops == ledger[jss::total_coins].asString() &&
            outCloseTime == ledger[jss::close_time].asUInt() &&
            outParentCloseTime == ledger[jss::parent_close_time].asUInt() &&
            outCloseTimeResolution ==
            ledger[jss::close_time_resolution].asUInt() &&
            outCloseFlags == ledger[jss::close_flags].asUInt() &&
            outAccountHash == ledger[jss::account_hash].asString() &&
            outTxHash == ledger[jss::transaction_hash].asString();
    }

    bool
    bad(Json::Value const& json, error_code_i error = rpcLGR_NOT_FOUND)
    {
        return json.isMember(jss::result) &&
            RPC::contains_error(json[jss::result]) &&
            json[jss::result][jss::error_code] == error;
    }

    std::string
    getHash(Json::Value const& json)
    {
        BEAST_EXPECT(
            json.isMember(jss::result) &&
            json[jss::result].isMember(jss::ledger) &&
            json[jss::result][jss::ledger].isMember(jss::ledger_hash) &&
            json[jss::result][jss::ledger][jss::ledger_hash].isString());
        return json[jss::result][jss::ledger][jss::ledger_hash].asString();
    }

    void
    ledgerCheck(jtx::Env& env, int const rows, int const first)
    {
        auto const [actualRows, actualFirst, actualLast] =
            dynamic_cast<SQLiteDatabase*>(&env.app().getRelationalDatabase())
                ->getLedgerCountMinMax();

        BEAST_EXPECT(actualRows == rows);
        BEAST_EXPECT(actualFirst == first);
        BEAST_EXPECT(actualLast == first + rows - 1);
    }

    void
    transactionCheck(jtx::Env& env, int const rows)
    {
        BEAST_EXPECT(
            dynamic_cast<SQLiteDatabase*>(&env.app().getRelationalDatabase())
                ->getTransactionCount() == rows);
    }

    void
    accountTransactionCheck(jtx::Env& env, int const rows)
    {
        BEAST_EXPECT(
            dynamic_cast<SQLiteDatabase*>(&env.app().getRelationalDatabase())
                ->getAccountTransactionCount() == rows);
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

        std::map<std::uint32_t, Json::Value const> ledgers;

        auto ledgerTmp = env.rpc("ledger", "0");
        BEAST_EXPECT(bad(ledgerTmp));

        ledgers.emplace(std::make_pair(1, env.rpc("ledger", "1")));
        BEAST_EXPECT(goodLedger(env, ledgers[1], "1"));

        ledgers.emplace(std::make_pair(2, env.rpc("ledger", "2")));
        BEAST_EXPECT(goodLedger(env, ledgers[2], "2"));

        ledgerTmp = env.rpc("ledger", "current");
        BEAST_EXPECT(goodLedger(env, ledgerTmp, "3"));

        ledgerTmp = env.rpc("ledger", "4");
        BEAST_EXPECT(bad(ledgerTmp));

        ledgerTmp = env.rpc("ledger", "100");
        BEAST_EXPECT(bad(ledgerTmp));

        auto const firstSeq = waitForReady(env);
        auto lastRotated = firstSeq - 1;

        for (auto i = firstSeq + 1; i < deleteInterval + firstSeq; ++i)
        {
            env.fund(XRP(10000), noripple("test" + std::to_string(i)));
            env.close();

            ledgerTmp = env.rpc("ledger", "current");
            BEAST_EXPECT(goodLedger(env, ledgerTmp, std::to_string(i)));
        }
        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        for (auto i = 3; i < deleteInterval + lastRotated; ++i)
        {
            ledgers.emplace(
                std::make_pair(i, env.rpc("ledger", std::to_string(i))));
            BEAST_EXPECT(
                goodLedger(env, ledgers[i], std::to_string(i), true) &&
                getHash(ledgers[i]).length());
        }

        ledgerCheck(env, deleteInterval + 1, 2);
        transactionCheck(env, deleteInterval);
        accountTransactionCheck(env, 2 * deleteInterval);

        {
            // Closing one more ledger triggers a rotate
            env.close();

            auto ledger = env.rpc("ledger", "current");
            BEAST_EXPECT(
                goodLedger(env, ledger, std::to_string(deleteInterval + 4)));
        }

        store.rendezvous();

        BEAST_EXPECT(store.getLastRotated() == deleteInterval + 3);
        lastRotated = store.getLastRotated();
        BEAST_EXPECT(lastRotated == 11);

        // That took care of the fake hashes
        ledgerCheck(env, deleteInterval + 1, 3);
        transactionCheck(env, deleteInterval);
        accountTransactionCheck(env, 2 * deleteInterval);

        // The last iteration of this loop should trigger a rotate
        for (auto i = lastRotated - 1; i < lastRotated + deleteInterval - 1;
             ++i)
        {
            env.close();

            ledgerTmp = env.rpc("ledger", "current");
            BEAST_EXPECT(goodLedger(env, ledgerTmp, std::to_string(i + 3)));

            ledgers.emplace(
                std::make_pair(i, env.rpc("ledger", std::to_string(i))));
            BEAST_EXPECT(
                store.getLastRotated() == lastRotated ||
                i == lastRotated + deleteInterval - 2);
            BEAST_EXPECT(
                goodLedger(env, ledgers[i], std::to_string(i), true) &&
                getHash(ledgers[i]).length());
        }

        store.rendezvous();

        BEAST_EXPECT(store.getLastRotated() == deleteInterval + lastRotated);

        ledgerCheck(env, deleteInterval + 1, lastRotated);
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
        BEAST_EXPECT(bad(canDelete, rpcNOT_ENABLED));

        // Close ledgers without triggering a rotate
        for (; ledgerSeq < lastRotated + deleteInterval; ++ledgerSeq)
        {
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(
                goodLedger(env, ledger, std::to_string(ledgerSeq), true));
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
            BEAST_EXPECT(
                goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        store.rendezvous();

        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);
        BEAST_EXPECT(lastRotated != store.getLastRotated());

        lastRotated = store.getLastRotated();

        // Close enough ledgers to trigger another rotate
        for (; ledgerSeq < lastRotated + deleteInterval + 1; ++ledgerSeq)
        {
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(
                goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        store.rendezvous();

        ledgerCheck(env, deleteInterval + 1, lastRotated);
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
        BEAST_EXPECT(!RPC::contains_error(canDelete[jss::result]));
        BEAST_EXPECT(canDelete[jss::result][jss::can_delete] == 0);

        canDelete = env.rpc("can_delete", "never");
        BEAST_EXPECT(!RPC::contains_error(canDelete[jss::result]));
        BEAST_EXPECT(canDelete[jss::result][jss::can_delete] == 0);

        auto const firstBatch = deleteInterval + ledgerSeq;
        for (; ledgerSeq < firstBatch; ++ledgerSeq)
        {
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(
                goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        store.rendezvous();

        ledgerCheck(env, ledgerSeq - 2, 2);
        BEAST_EXPECT(lastRotated == store.getLastRotated());

        // This does not kick off a cleanup
        canDelete = env.rpc(
            "can_delete", std::to_string(ledgerSeq + deleteInterval / 2));
        BEAST_EXPECT(!RPC::contains_error(canDelete[jss::result]));
        BEAST_EXPECT(
            canDelete[jss::result][jss::can_delete] ==
            ledgerSeq + deleteInterval / 2);

        store.rendezvous();

        ledgerCheck(env, ledgerSeq - 2, 2);
        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off a cleanup, but it stays small.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(
                goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        store.rendezvous();

        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);

        BEAST_EXPECT(store.getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;

        for (; ledgerSeq < lastRotated + deleteInterval; ++ledgerSeq)
        {
            // No cleanups in this loop.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(
                goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        store.rendezvous();

        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off another cleanup.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(
                goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        store.rendezvous();

        ledgerCheck(env, ledgerSeq - firstBatch, firstBatch);

        BEAST_EXPECT(store.getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;

        // This does not kick off a cleanup
        canDelete = env.rpc("can_delete", "always");
        BEAST_EXPECT(!RPC::contains_error(canDelete[jss::result]));
        BEAST_EXPECT(
            canDelete[jss::result][jss::can_delete] ==
            std::numeric_limits<unsigned int>::max());

        for (; ledgerSeq < lastRotated + deleteInterval; ++ledgerSeq)
        {
            // No cleanups in this loop.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(
                goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        store.rendezvous();

        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off another cleanup.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(
                goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        store.rendezvous();

        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);

        BEAST_EXPECT(store.getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;

        // This does not kick off a cleanup
        canDelete = env.rpc("can_delete", "now");
        BEAST_EXPECT(!RPC::contains_error(canDelete[jss::result]));
        BEAST_EXPECT(canDelete[jss::result][jss::can_delete] == ledgerSeq - 1);

        for (; ledgerSeq < lastRotated + deleteInterval; ++ledgerSeq)
        {
            // No cleanups in this loop.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(
                goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        store.rendezvous();

        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off another cleanup.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(
                goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        store.rendezvous();

        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);

        BEAST_EXPECT(store.getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;
    }

    std::unique_ptr<NodeStore::Backend>
    makeBackendRotating(
        jtx::Env& env,
        NodeStoreScheduler& scheduler,
        std::string path)
    {
        Section section{
            env.app().config().section(ConfigSection::nodeDatabase())};
        boost::filesystem::path newPath;

        if (!BEAST_EXPECT(path.size()))
            return {};
        newPath = path;
        section.set("path", newPath.string());

        auto backend{NodeStore::Manager::instance().make_Backend(
            section,
            megabytes(env.app().config().getValueFor(
                SizedItem::burstSize, std::nullopt)),
            scheduler,
            env.app().logs().journal("NodeStoreTest"))};
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
        // Create the backend. Normally, SHAMapStoreImp handles all these
        // details
        auto nscfg = env.app().config().section(ConfigSection::nodeDatabase());

        // Provide default values:
        if (!nscfg.exists("cache_size"))
            nscfg.set(
                "cache_size",
                std::to_string(env.app().config().getValueFor(
                    SizedItem::treeCacheSize, std::nullopt)));

        if (!nscfg.exists("cache_age"))
            nscfg.set(
                "cache_age",
                std::to_string(env.app().config().getValueFor(
                    SizedItem::treeCacheAge, std::nullopt)));

        NodeStoreScheduler scheduler(env.app().getJobQueue());

        std::string const writableDb = "write";
        std::string const archiveDb = "archive";
        auto writableBackend = makeBackendRotating(env, scheduler, writableDb);
        auto archiveBackend = makeBackendRotating(env, scheduler, archiveDb);

        // Create NodeStore with two backends to allow online deletion of
        // data
        constexpr int readThreads = 4;
        auto dbr = std::make_unique<NodeStore::DatabaseRotatingImp>(
            scheduler,
            readThreads,
            std::move(writableBackend),
            std::move(archiveBackend),
            nscfg,
            env.app().logs().journal("NodeStoreTest"));

        /////////////////////////////////////////////////////////////
        // Check basic functionality
        using namespace std::chrono_literals;
        std::atomic<int> threadNum = 0;

        {
            auto newBackend = makeBackendRotating(
                env, scheduler, std::to_string(++threadNum));

            auto const cb = [&](std::string const& writableName,
                                std::string const& archiveName) {
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
            auto const cb = [&](std::string const& writableName,
                                std::string const& archiveName) {
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
                auto newBackend = makeBackendRotating(
                    env, scheduler, std::to_string(++threadNum));
                // Reminder: doing this is stupid and should never happen
                dbr->rotate(std::move(newBackend), cb);
            };
            auto newBackend = makeBackendRotating(
                env, scheduler, std::to_string(++threadNum));
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
            hashes.emplace(
                env.current()->info().seq, env.current()->info().hash);

            auto const& root =
                ns.fetchNodeObject(hashes[env.current()->info().seq]);
            BEAST_EXPECT(root);
        };

        auto failureMessage = [&](char const* label,
                                  auto expected,
                                  auto actual) {
            std::stringstream ss;
            ss << label << ": Expected: " << expected << ", Got: " << actual;
            return ss.str();
        };

        auto const alice = Account("alice");
        env.fund(XRP(1000), alice);
        env.close();

        auto& lm = env.app().getLedgerMaster();
        LedgerIndex minSeq = 2;
        LedgerIndex maxSeq = env.closed()->info().seq;
        auto& store = env.app().getSHAMapStore();
        LedgerIndex lastRotated = store.getLastRotated();
        BEAST_EXPECTS(maxSeq == 3, to_string(maxSeq));
        BEAST_EXPECTS(
            lm.getCompleteLedgers() == "2-3", lm.getCompleteLedgers());
        BEAST_EXPECTS(lastRotated == 3, to_string(lastRotated));
        BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq, maxSeq) == 0);
        BEAST_EXPECT(
            lm.missingFromCompleteLedgerRange(minSeq + 1, maxSeq - 1) == 0);
        BEAST_EXPECT(
            lm.missingFromCompleteLedgerRange(minSeq - 1, maxSeq + 1) == 2);
        BEAST_EXPECT(
            lm.missingFromCompleteLedgerRange(minSeq - 2, maxSeq - 2) == 2);
        BEAST_EXPECT(
            lm.missingFromCompleteLedgerRange(minSeq + 2, maxSeq + 2) == 2);

        // Close enough ledgers to rotate a few times
        while (maxSeq < 20)
        {
            for (int t = 0; t < 3; ++t)
            {
                env(noop(alice));
            }
            env.close();
            store.rendezvous();

            ++maxSeq;

            if (maxSeq + 1 == lastRotated + deleteInterval)
            {
                // The next ledger will trigger a rotation. Delete an arbitrary
                // ledger from LedgerMaster.
                LedgerIndex const deleteSeq = maxSeq;
                lm.clearLedger(deleteSeq);

                auto expectedRange =
                    [](auto minSeq, auto deleteSeq, auto maxSeq) {
                        std::stringstream expectedRange;
                        expectedRange << minSeq << "-" << (deleteSeq - 1);
                        if (deleteSeq + 1 == maxSeq)
                            expectedRange << "," << maxSeq;
                        else if (deleteSeq < maxSeq)
                            expectedRange << "," << (deleteSeq + 1) << "-"
                                          << maxSeq;
                        return expectedRange.str();
                    };
                BEAST_EXPECTS(
                    lm.getCompleteLedgers() ==
                        expectedRange(minSeq, deleteSeq, maxSeq),
                    failureMessage(
                        "Complete ledgers",
                        expectedRange(minSeq, deleteSeq, maxSeq),
                        lm.getCompleteLedgers()));
                BEAST_EXPECT(
                    lm.missingFromCompleteLedgerRange(minSeq, maxSeq) == 1);

                // Close another ledger, which will trigger a rotation, but the
                // rotation will be stuck until the missing ledger is filled in.
                env.close();
                // DO NOT CALL rendezvous()! You'll end up with a deadlock.
                ++maxSeq;

                // Nothing has changed
                BEAST_EXPECTS(
                    store.getLastRotated() == lastRotated,
                    failureMessage(
                        "lastRotated", lastRotated, store.getLastRotated()));
                BEAST_EXPECTS(
                    lm.getCompleteLedgers() ==
                        expectedRange(minSeq, deleteSeq, maxSeq),
                    failureMessage(
                        "Complete ledgers",
                        expectedRange(minSeq, deleteSeq, maxSeq),
                        lm.getCompleteLedgers()));

                using namespace std::chrono_literals;
                // Close 5 more ledgers, waiting one second in between to
                // simulate the ledger making progress while online delete waits
                // for the missing ledger to be filled in.
                // This ensures the healthWait check has time to run and
                // detect the gap.
                for (int l = 0; l < 5; ++l)
                {
                    env.close();
                    // DO NOT CALL rendezvous()! You'll end up with a deadlock.
                    ++maxSeq;
                    // Nothing has changed
                    BEAST_EXPECTS(
                        store.getLastRotated() == lastRotated,
                        failureMessage(
                            "lastRotated",
                            lastRotated,
                            store.getLastRotated()));
                    BEAST_EXPECTS(
                        lm.getCompleteLedgers() ==
                            expectedRange(minSeq, deleteSeq, maxSeq),
                        failureMessage(
                            "Complete Ledgers",
                            expectedRange(minSeq, deleteSeq, maxSeq),
                            lm.getCompleteLedgers()));
                    std::this_thread::sleep_for(1s);
                }

                // Put the missing ledger back in LedgerMaster
                lm.setLedgerRangePresent(deleteSeq, deleteSeq);

                // Wait for the rotation to finish
                store.rendezvous();

                minSeq = lastRotated;
                lastRotated = deleteSeq + 1;
            }
            BEAST_EXPECT(maxSeq != lastRotated + deleteInterval);
            BEAST_EXPECTS(
                env.closed()->info().seq == maxSeq,
                failureMessage("maxSeq", maxSeq, env.closed()->info().seq));
            BEAST_EXPECTS(
                store.getLastRotated() == lastRotated,
                failureMessage(
                    "lastRotated", lastRotated, store.getLastRotated()));
            std::stringstream expectedRange;
            expectedRange << minSeq << "-" << maxSeq;
            BEAST_EXPECTS(
                lm.getCompleteLedgers() == expectedRange.str(),
                failureMessage(
                    "CompleteLedgers",
                    expectedRange.str(),
                    lm.getCompleteLedgers()));
            BEAST_EXPECT(
                lm.missingFromCompleteLedgerRange(minSeq, maxSeq) == 0);
            BEAST_EXPECT(
                lm.missingFromCompleteLedgerRange(minSeq + 1, maxSeq - 1) == 0);
            BEAST_EXPECT(
                lm.missingFromCompleteLedgerRange(minSeq - 1, maxSeq + 1) == 2);
            BEAST_EXPECT(
                lm.missingFromCompleteLedgerRange(minSeq - 2, maxSeq - 2) == 2);
            BEAST_EXPECT(
                lm.missingFromCompleteLedgerRange(minSeq + 2, maxSeq + 2) == 2);
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
BEAST_DEFINE_TESTSUITE(SHAMapStore, app, ripple);

}  // namespace test
}  // namespace ripple
