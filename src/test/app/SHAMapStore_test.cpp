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

#include <BeastConfig.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/SHAMapStore.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/SociDB.h>
#include <ripple/protocol/JsonFields.h>
#include <test/jtx.h>

namespace ripple {
namespace test {

class SHAMapStore_test : public beast::unit_test::suite
{
    static auto const deleteInterval = 8;

    static
    std::unique_ptr<Config>
    makeConfig()
    {
        auto p = std::make_unique<Config>();
        setupConfigForUnitTests(*p);
        p->LEDGER_HISTORY = deleteInterval;
        auto& section = p->section(ConfigSection::nodeDatabase());
        section.set("online_delete", to_string(deleteInterval));
        //section.set("age_threshold", "60");
        return p;
    }

    static
    std::unique_ptr<Config>
    makeConfigAdvisory()
    {
        auto p = makeConfig();
        auto& section = p->section(ConfigSection::nodeDatabase());
        section.set("advisory_delete", "1");
        return p;
    }

    bool goodLedger(jtx::Env& env, Json::Value const& json,
        std::string ledgerID, bool checkDB = false)
    {
        auto good = json.isMember(jss::result)
            && !RPC::contains_error(json[jss::result])
            && json[jss::result][jss::ledger][jss::ledger_index] == ledgerID;
        if (!good || !checkDB)
            return good;

        auto const seq = json[jss::result][jss::ledger_index].asUInt();
        std::string outHash;
        LedgerIndex outSeq;
        std::string outParentHash;
        std::string outDrops;
        std::uint64_t outCloseTime;
        std::uint64_t outParentCloseTime;
        std::uint64_t outCloseTimeResolution;
        std::uint64_t outCloseFlags;
        std::string outAccountHash;
        std::string outTxHash;

        {
            auto db = env.app().getLedgerDB().checkoutDb();

            *db << "SELECT LedgerHash,LedgerSeq,PrevHash,TotalCoins, "
                "ClosingTime,PrevClosingTime,CloseTimeRes,CloseFlags, "
                "AccountSetHash,TransSetHash "
                "FROM Ledgers "
                "WHERE LedgerSeq = :seq",
                soci::use(seq),
                soci::into(outHash),
                soci::into(outSeq),
                soci::into(outParentHash),
                soci::into(outDrops),
                soci::into(outCloseTime),
                soci::into(outParentCloseTime),
                soci::into(outCloseTimeResolution),
                soci::into(outCloseFlags),
                soci::into(outAccountHash),
                soci::into(outTxHash);
        }

        auto const& ledger = json[jss::result][jss::ledger];
        return outHash == ledger[jss::hash].asString() &&
            outSeq == seq &&
            outParentHash == ledger[jss::parent_hash].asString() &&
            outDrops == ledger[jss::total_coins].asString() &&
            outCloseTime == ledger[jss::close_time].asUInt() &&
            outParentCloseTime == ledger[jss::parent_close_time].asUInt() &&
            outCloseTimeResolution == ledger[jss::close_time_resolution].asUInt() &&
            outCloseFlags == ledger[jss::close_flags].asUInt() &&
            outAccountHash == ledger[jss::account_hash].asString() &&
            outTxHash == ledger[jss::transaction_hash].asString();
    }

    bool bad(Json::Value const& json, error_code_i error = rpcLGR_NOT_FOUND)
    {
        return json.isMember(jss::result)
            && RPC::contains_error(json[jss::result])
            && json[jss::result][jss::error_code] == error;
    }

    std::string getHash(Json::Value const& json)
    {
        BEAST_EXPECT(json.isMember(jss::result) &&
            json[jss::result].isMember(jss::ledger) &&
            json[jss::result][jss::ledger].isMember(jss::hash) &&
            json[jss::result][jss::ledger][jss::hash].isString());
        return json[jss::result][jss::ledger][jss::hash].asString();
    }

    void validationCheck(jtx::Env& env, int const expected)
    {
        auto db = env.app().getLedgerDB().checkoutDb();

        int actual;
        *db << "SELECT count(*) AS rows FROM Validations;",
            soci::into(actual);

        BEAST_EXPECT(actual == expected);

    }

    void ledgerCheck(jtx::Env& env, int const rows,
        int const first)
    {
        auto db = env.app().getLedgerDB().checkoutDb();

        int actualRows, actualFirst, actualLast;
        *db << "SELECT count(*) AS rows, "
            "min(LedgerSeq) as first, "
            "max(LedgerSeq) as last "
            "FROM Ledgers;",
            soci::into(actualRows),
            soci::into(actualFirst),
            soci::into(actualLast);

        BEAST_EXPECT(actualRows == rows);
        BEAST_EXPECT(actualFirst == first);
        BEAST_EXPECT(actualLast == first + rows - 1);

    }

    void transactionCheck(jtx::Env& env, int const rows)
    {
        auto db = env.app().getTxnDB().checkoutDb();

        int actualRows;
        *db << "SELECT count(*) AS rows "
            "FROM Transactions;",
            soci::into(actualRows);

        BEAST_EXPECT(actualRows == rows);
    }

    void accountTransactionCheck(jtx::Env& env, int const rows)
    {
        auto db = env.app().getTxnDB().checkoutDb();

        int actualRows;
        *db << "SELECT count(*) AS rows "
            "FROM AccountTransactions;",
            soci::into(actualRows);

        BEAST_EXPECT(actualRows == rows);
    }

    int waitForReady(jtx::Env& env)
    {
        using namespace std::chrono_literals;

        auto& store = env.app().getSHAMapStore();

        int ledgerSeq = 3;
        store.rendezvous();
        BEAST_EXPECT(!store.getLastRotated());

        env.close();
        store.rendezvous();

        auto ledger = env.rpc("ledger", "validated");
        BEAST_EXPECT(goodLedger(env, ledger, to_string(ledgerSeq++)));

        BEAST_EXPECT(store.getLastRotated() == ledgerSeq - 1);
        return ledgerSeq;
    }

public:
    void testClear()
    {
        using namespace std::chrono_literals;

        testcase("clearPrior");
        using namespace jtx;

        Env env(*this, makeConfig());

        auto& store = env.app().getSHAMapStore();
        env.fund(XRP(10000), noripple("alice"));

        validationCheck(env, 0);
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
            env.fund(XRP(10000), noripple("test" + to_string(i)));
            env.close();

            ledgerTmp = env.rpc("ledger", "current");
            BEAST_EXPECT(goodLedger(env, ledgerTmp, to_string(i)));
        }
        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        for (auto i = 3; i < deleteInterval + lastRotated; ++i)
        {
            ledgers.emplace(std::make_pair(i,
                env.rpc("ledger", to_string(i))));
            BEAST_EXPECT(goodLedger(env, ledgers[i], to_string(i), true) &&
                getHash(ledgers[i]).length());
        }

        validationCheck(env, 0);
        ledgerCheck(env, deleteInterval + 1, 2);
        transactionCheck(env, deleteInterval);
        accountTransactionCheck(env, 2 * deleteInterval);

        {
            // Since standalone doesn't _do_ validations, manually
            // insert some into the table. Create some with the
            // hashes from our real ledgers, and some with fake
            // hashes to represent validations that never ended up
            // in a validated ledger.
            char lh[65];
            memset(lh, 'a', 64);
            lh[64] = '\0';
            std::vector<std::string> preSeqLedgerHashes({
                lh
            });
            std::vector<std::string> badLedgerHashes;
            std::vector<LedgerIndex> badLedgerSeqs;
            std::vector<std::string> ledgerHashes;
            std::vector<LedgerIndex> ledgerSeqs;
            for (auto const& lgr : ledgers)
            {
                ledgerHashes.emplace_back(getHash(lgr.second));
                ledgerSeqs.emplace_back(lgr.second[jss::result][jss::ledger_index].asUInt());
            }
            for (auto i = 0; i < 10; ++i)
            {
                ++lh[30];
                preSeqLedgerHashes.emplace_back(lh);
                ++lh[20];
                badLedgerHashes.emplace_back(lh);
                badLedgerSeqs.emplace_back(i + 1);
            }

            auto db = env.app().getLedgerDB().checkoutDb();

            // Pre-migration validation - no sequence numbers.
            *db << "INSERT INTO Validations "
                "(LedgerHash) "
                "VALUES "
                "(:ledgerHash);",
                soci::use(preSeqLedgerHashes);
            // Post-migration orphan validation - InitalSeq,
            // but no LedgerSeq
            *db << "INSERT INTO Validations "
                "(LedgerHash, InitialSeq) "
                "VALUES "
                "(:ledgerHash, :initialSeq);",
                soci::use(badLedgerHashes),
                soci::use(badLedgerSeqs);
            // Post-migration validated ledger.
            *db << "INSERT INTO Validations "
                "(LedgerHash, LedgerSeq) "
                "VALUES "
                "(:ledgerHash, :ledgerSeq);",
                soci::use(ledgerHashes),
                soci::use(ledgerSeqs);
        }

        validationCheck(env, deleteInterval + 23);
        ledgerCheck(env, deleteInterval + 1, 2);
        transactionCheck(env, deleteInterval);
        accountTransactionCheck(env, 2 * deleteInterval);

        {
            // Closing one more ledger triggers a rotate
            env.close();

            auto ledger = env.rpc("ledger", "current");
            BEAST_EXPECT(goodLedger(env, ledger, to_string(deleteInterval + 4)));
        }

        store.rendezvous();

        BEAST_EXPECT(store.getLastRotated() == deleteInterval + 3);
        lastRotated = store.getLastRotated();
        BEAST_EXPECT(lastRotated == 11);

        // That took care of the fake hashes
        validationCheck(env, deleteInterval + 8);
        ledgerCheck(env, deleteInterval + 1, 3);
        transactionCheck(env, deleteInterval);
        accountTransactionCheck(env, 2 * deleteInterval);

        // The last iteration of this loop should trigger a rotate
        for (auto i = lastRotated - 1; i < lastRotated + deleteInterval - 1; ++i)
        {
            validationCheck(env, deleteInterval + i + 1 - lastRotated + 8);

            env.close();

            ledgerTmp = env.rpc("ledger", "current");
            BEAST_EXPECT(goodLedger(env, ledgerTmp, to_string(i + 3)));

            ledgers.emplace(std::make_pair(i,
                env.rpc("ledger", to_string(i))));
            BEAST_EXPECT(store.getLastRotated() == lastRotated ||
                i == lastRotated + deleteInterval - 2);
            BEAST_EXPECT(goodLedger(env, ledgers[i], to_string(i), true) &&
                getHash(ledgers[i]).length());

            std::vector<std::string> ledgerHashes({
                getHash(ledgers[i])
            });
            std::vector<LedgerIndex> ledgerSeqs({
                ledgers[i][jss::result][jss::ledger_index].asUInt()
            });
            auto db = env.app().getLedgerDB().checkoutDb();

            *db << "INSERT INTO Validations "
                "(LedgerHash, LedgerSeq) "
                "VALUES "
                "(:ledgerHash, :ledgerSeq);",
                soci::use(ledgerHashes),
                soci::use(ledgerSeqs);
        }

        store.rendezvous();

        BEAST_EXPECT(store.getLastRotated() == deleteInterval + lastRotated);

        validationCheck(env, deleteInterval - 1);
        ledgerCheck(env, deleteInterval + 1, lastRotated);
        transactionCheck(env, 0);
        accountTransactionCheck(env, 0);

    }

    void testAutomatic()
    {
        testcase("automatic online_delete");
        using namespace jtx;
        using namespace std::chrono_literals;

        Env env(*this, makeConfig());
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
            BEAST_EXPECT(goodLedger(env, ledger, to_string(ledgerSeq), true));
        }

        store.rendezvous();

        // The database will always have back to ledger 2,
        // regardless of lastRotated.
        validationCheck(env, 0);
        ledgerCheck(env, ledgerSeq - 2, 2);
        BEAST_EXPECT(lastRotated == store.getLastRotated());

        {
            // Closing one more ledger triggers a rotate
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, to_string(ledgerSeq++), true));
        }

        store.rendezvous();

        validationCheck(env, 0);
        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);
        BEAST_EXPECT(lastRotated != store.getLastRotated());

        lastRotated = store.getLastRotated();

        // Close enough ledgers to trigger another rotate
        for (; ledgerSeq < lastRotated + deleteInterval + 1; ++ledgerSeq)
        {
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, to_string(ledgerSeq), true));
        }

        store.rendezvous();

        validationCheck(env, 0);
        ledgerCheck(env, deleteInterval + 1, lastRotated);
        BEAST_EXPECT(lastRotated != store.getLastRotated());
    }

    void testCanDelete()
    {
        testcase("online_delete with advisory_delete");
        using namespace jtx;
        using namespace std::chrono_literals;

        // Same config with advisory_delete enabled
        Env env(*this, makeConfigAdvisory());
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
            BEAST_EXPECT(goodLedger(env, ledger, to_string(ledgerSeq), true));
        }

        store.rendezvous();

        validationCheck(env, 0);
        ledgerCheck(env, ledgerSeq - 2, 2);
        BEAST_EXPECT(lastRotated == store.getLastRotated());

        // This does not kick off a cleanup
        canDelete = env.rpc("can_delete", to_string(
            ledgerSeq + deleteInterval / 2));
        BEAST_EXPECT(!RPC::contains_error(canDelete[jss::result]));
        BEAST_EXPECT(canDelete[jss::result][jss::can_delete] ==
            ledgerSeq + deleteInterval / 2);

        store.rendezvous();

        validationCheck(env, 0);
        ledgerCheck(env, ledgerSeq - 2, 2);
        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off a cleanup, but it stays small.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, to_string(ledgerSeq++), true));
        }

        store.rendezvous();

        validationCheck(env, 0);
        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);

        BEAST_EXPECT(store.getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;

        for (; ledgerSeq < lastRotated + deleteInterval; ++ledgerSeq)
        {
            // No cleanups in this loop.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, to_string(ledgerSeq), true));
        }

        store.rendezvous();

        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off another cleanup.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, to_string(ledgerSeq++), true));
        }

        store.rendezvous();

        validationCheck(env, 0);
        ledgerCheck(env, ledgerSeq - firstBatch, firstBatch);

        BEAST_EXPECT(store.getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;

        // This does not kick off a cleanup
        canDelete = env.rpc("can_delete", "always");
        BEAST_EXPECT(!RPC::contains_error(canDelete[jss::result]));
        BEAST_EXPECT(canDelete[jss::result][jss::can_delete] ==
            std::numeric_limits <unsigned int>::max());

        for (; ledgerSeq < lastRotated + deleteInterval; ++ledgerSeq)
        {
            // No cleanups in this loop.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, to_string(ledgerSeq), true));
        }

        store.rendezvous();

        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off another cleanup.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, to_string(ledgerSeq++), true));
        }

        store.rendezvous();

        validationCheck(env, 0);
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
            BEAST_EXPECT(goodLedger(env, ledger, to_string(ledgerSeq), true));
        }

        store.rendezvous();

        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off another cleanup.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, to_string(ledgerSeq++), true));
        }

        store.rendezvous();

        validationCheck(env, 0);
        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);

        BEAST_EXPECT(store.getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;
    }

    void run()
    {
        testClear();
        testAutomatic();
        testCanDelete();
    }
};

// VFALCO This test fails because of thread asynchronous issues
BEAST_DEFINE_TESTSUITE(SHAMapStore,app,ripple);

}
}
