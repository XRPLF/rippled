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
#include <ripple/app/misc/SHAMapStoreImp.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/test/jtx.h>

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

    bool goodLedger(Json::Value const& json, std::string ledgerID)
    {
        return json.isMember(jss::result)
            && !RPC::contains_error(json[jss::result])
            && json[jss::result][jss::ledger][jss::ledger_index] == ledgerID;
    }

    bool bad(Json::Value const& json, error_code_i error = rpcLGR_NOT_FOUND)
    {
        return json.isMember(jss::result)
            && RPC::contains_error(json[jss::result])
            && json[jss::result][jss::error_code] == error;
    }

    std::string getHash(Json::Value const& json)
    {
        expect(json.isMember(jss::result) &&
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

        expect(actual == expected);

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

        expect(actualRows == rows);
        expect(actualFirst == first);
        expect(actualLast == first + rows - 1);

    }

    void transactionCheck(jtx::Env& env, int const rows)
    {
        auto db = env.app().getTxnDB().checkoutDb();

        int actualRows;
        *db << "SELECT count(*) AS rows "
            "FROM Transactions;",
            soci::into(actualRows);

        expect(actualRows == rows);
    }

    void accountTransactionCheck(jtx::Env& env, int const rows)
    {
        auto db = env.app().getTxnDB().checkoutDb();

        int actualRows;
        *db << "SELECT count(*) AS rows "
            "FROM AccountTransactions;",
            soci::into(actualRows);

        expect(actualRows == rows);
    }

    int waitForReady(jtx::Env& env)
    {
        using namespace std::chrono_literals;

        auto& store = env.app().getSHAMapStore();

        int ledgerSeq = 3;
        while (!store.getLastRotated())
        {
            env.close();
            std::this_thread::sleep_for(100ms);

            auto ledger = env.rpc("ledger", "validated");
            expect(goodLedger(ledger, to_string(ledgerSeq++)));
        }
        return ledgerSeq;
    }

public:
    void testClear()
    {
        using namespace std::chrono_literals;

        testcase("clearPrior");
        using namespace jtx;

        Env env(*this, makeConfig());

        auto store = dynamic_cast<SHAMapStoreImp*>(
            &env.app().getSHAMapStore());
        expect(store);
        env.fund(XRP(10000), noripple("alice"));

        validationCheck(env, 0);
        ledgerCheck(env, 1, 2);
        transactionCheck(env, 0);
        accountTransactionCheck(env, 0);

        std::map<std::uint32_t, Json::Value const> ledgers;

        auto ledgerTmp = env.rpc("ledger", "0");
        expect(bad(ledgerTmp));

        ledgers.emplace(std::make_pair(1, env.rpc("ledger", "1")));
        expect(goodLedger(ledgers[1], "1"));

        ledgers.emplace(std::make_pair(2, env.rpc("ledger", "2")));
        expect(goodLedger(ledgers[2], "2"));

        ledgerTmp = env.rpc("ledger", "current");
        expect(goodLedger(ledgerTmp, "3"));

        ledgerTmp = env.rpc("ledger", "4");
        expect(bad(ledgerTmp));

        ledgerTmp = env.rpc("ledger", "100");
        expect(bad(ledgerTmp));

        for (auto i = 4; i < deleteInterval + 4; ++i)
        {
            env.fund(XRP(10000), noripple("test" + to_string(i)));
            env.close();

            ledgerTmp = env.rpc("ledger", "current");
            expect(goodLedger(ledgerTmp, to_string(i)));
        }
        assert(store->getLastRotated() == 3);

        for (auto i = 3; i < deleteInterval + 3; ++i)
        {
            ledgers.emplace(std::make_pair(i,
                env.rpc("ledger", to_string(i))));
            expect(goodLedger(ledgers[i], to_string(i)) &&
                getHash(ledgers[i]).length());
        }

        validationCheck(env, 0);
        ledgerCheck(env, deleteInterval + 1, 2);
        transactionCheck(env, deleteInterval + 1);
        accountTransactionCheck(env, 2*(deleteInterval + 1));

        {
            // Since standalone doesn't _do_ validations, manually
            // insert some into the table. Create some with the
            // hashes from our real ledgers, and some with fake
            // hashes to represent validations that never ended up
            // in a validated ledger.
            char lh[65];
            memset(lh, 'a', 64);
            lh[64] = '\0';
            std::vector<std::string> ledgerHashes({
                lh
            });
            for (auto const& lgr : ledgers)
            {
                ledgerHashes.emplace_back(getHash(lgr.second));
            }
            for (auto i = 0; i < 10; ++i)
            {
                ++lh[30];
                ledgerHashes.emplace_back(lh);
            }

            auto db = env.app().getLedgerDB().checkoutDb();

            *db << "INSERT INTO Validations "
                "(LedgerHash) "
                "VALUES "
                "(:ledgerHash);",
                soci::use(ledgerHashes);
        }

        validationCheck(env, deleteInterval + 13);
        ledgerCheck(env, deleteInterval + 1, 2);
        transactionCheck(env, deleteInterval + 1);
        accountTransactionCheck(env, 2 * (deleteInterval + 1));

        {
            // Closing one more ledger triggers a rotate
            env.close();

            auto ledger = env.rpc("ledger", "current");
            expect(goodLedger(ledger, to_string(deleteInterval + 4)));
        }

        while (store->rotating())
            std::this_thread::sleep_for(1ms);

        expect(store->getLastRotated() == deleteInterval + 3);
        auto const lastRotated = store->getLastRotated();

        // That took care of the fake hashes
        validationCheck(env, deleteInterval);
        ledgerCheck(env, deleteInterval + 1, 3);
        transactionCheck(env, deleteInterval + 1);
        accountTransactionCheck(env, 2 * (deleteInterval + 1));

        for (auto i = lastRotated - 1; i < lastRotated + deleteInterval - 1; ++i)
        {
            validationCheck(env, deleteInterval + i + 1 - lastRotated);

            env.close();

            ledgerTmp = env.rpc("ledger", "current");
            expect(goodLedger(ledgerTmp, to_string(i + 3)));

            ledgers.emplace(std::make_pair(i,
                env.rpc("ledger", to_string(i))));
            expect(goodLedger(ledgers[i], to_string(i)) &&
                getHash(ledgers[i]).length());

            std::vector<std::string> ledgerHashes({
                getHash(ledgers[i])
            });
            auto db = env.app().getLedgerDB().checkoutDb();

            *db << "INSERT INTO Validations "
                "(LedgerHash) "
                "VALUES "
                "(:ledgerHash);",
                soci::use(ledgerHashes);
        }

        while (store->rotating())
            std::this_thread::sleep_for(1ms);

        expect(store->getLastRotated() == deleteInterval + lastRotated);

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
        auto store = dynamic_cast<SHAMapStoreImp*>(
            &env.app().getSHAMapStore());
        expect(store);

        auto ledgerSeq = waitForReady(env);
        auto lastRotated = ledgerSeq - 1;
        expect(store->getLastRotated() == lastRotated,
            to_string(store->getLastRotated()));
        expect(lastRotated != 2);

        // Because advisory_delete is unset,
        // "can_delete" is disabled.
        auto const canDelete = env.rpc("can_delete");
        expect(bad(canDelete, rpcNOT_ENABLED));

        // Close ledgers without triggering a rotate
        for (; ledgerSeq < lastRotated + deleteInterval; ++ledgerSeq)
        {
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            expect(goodLedger(ledger, to_string(ledgerSeq)));
        }

        while(store->rotating())
            std::this_thread::sleep_for(1ms);

        // The database will always have back to ledger 2,
        // regardless of lastRotated.
        validationCheck(env, 0);
        ledgerCheck(env, ledgerSeq - 2, 2);
        expect(lastRotated == store->getLastRotated());

        {
            // Closing one more ledger triggers a rotate
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            expect(goodLedger(ledger, to_string(ledgerSeq++)));
        }

        while (store->rotating())
            std::this_thread::sleep_for(1ms);

        validationCheck(env, 0);
        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);
        expect(lastRotated != store->getLastRotated());

        lastRotated = store->getLastRotated();

        // Close enough ledgers to trigger another rotate
        for (; ledgerSeq < lastRotated + deleteInterval + 1; ++ledgerSeq)
        {
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            expect(goodLedger(ledger, to_string(ledgerSeq)));
        }

        while (store->rotating())
            std::this_thread::sleep_for(1ms);

        validationCheck(env, 0);
        ledgerCheck(env, deleteInterval + 1, lastRotated);
        expect(lastRotated != store->getLastRotated());
    }

    void testCanDelete()
    {
        testcase("online_delete with advisory_delete");
        using namespace jtx;
        using namespace std::chrono_literals;

        // Same config with advisory_delete enabled
        Env env(*this, makeConfigAdvisory());
        auto store = dynamic_cast<SHAMapStoreImp*>(
            &env.app().getSHAMapStore());
        expect(store);

        auto ledgerSeq = waitForReady(env);
        auto lastRotated = ledgerSeq - 1;
        expect(store->getLastRotated() == lastRotated,
            to_string(store->getLastRotated()));
        expect(lastRotated != 2);

        auto canDelete = env.rpc("can_delete");
        expect(!RPC::contains_error(canDelete[jss::result]));
        expect(canDelete[jss::result][jss::can_delete] == 0);

        canDelete = env.rpc("can_delete", "never");
        expect(!RPC::contains_error(canDelete[jss::result]));
        expect(canDelete[jss::result][jss::can_delete] == 0);

        auto const firstBatch = deleteInterval + ledgerSeq;
        for (; ledgerSeq < firstBatch; ++ledgerSeq)
        {
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            expect(goodLedger(ledger, to_string(ledgerSeq)));
        }

        while (store->rotating())
            std::this_thread::sleep_for(1ms);

        validationCheck(env, 0);
        ledgerCheck(env, ledgerSeq - 2, 2);
        expect(lastRotated == store->getLastRotated());

        // This does not kick off a cleanup
        canDelete = env.rpc("can_delete", to_string(
            ledgerSeq + deleteInterval / 2));
        expect(!RPC::contains_error(canDelete[jss::result]));
        expect(canDelete[jss::result][jss::can_delete] ==
            ledgerSeq + deleteInterval / 2);

        while (store->rotating())
            std::this_thread::sleep_for(1ms);

        validationCheck(env, 0);
        ledgerCheck(env, ledgerSeq - 2, 2);
        expect(store->getLastRotated() == lastRotated);

        {
            // This kicks off a cleanup, but it stays small.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            expect(goodLedger(ledger, to_string(ledgerSeq++)));
        }

        while (store->rotating())
            std::this_thread::sleep_for(1ms);

        validationCheck(env, 0);
        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);

        expect(store->getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;

        for (; ledgerSeq < lastRotated + deleteInterval; ++ledgerSeq)
        {
            // No cleanups in this loop.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            expect(goodLedger(ledger, to_string(ledgerSeq)));
        }

        while (store->rotating())
            std::this_thread::sleep_for(1ms);

        expect(store->getLastRotated() == lastRotated);

        {
            // This kicks off another cleanup.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            expect(goodLedger(ledger, to_string(ledgerSeq++)));
        }

        while (store->rotating())
            std::this_thread::sleep_for(1ms);

        validationCheck(env, 0);
        ledgerCheck(env, ledgerSeq - firstBatch, firstBatch);

        expect(store->getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;

        // This does not kick off a cleanup
        canDelete = env.rpc("can_delete", "always");
        expect(!RPC::contains_error(canDelete[jss::result]));
        expect(canDelete[jss::result][jss::can_delete] ==
            std::numeric_limits <unsigned int>::max());

        for (; ledgerSeq < lastRotated + deleteInterval; ++ledgerSeq)
        {
            // No cleanups in this loop.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            expect(goodLedger(ledger, to_string(ledgerSeq)));
        }

        while (store->rotating())
            std::this_thread::sleep_for(1ms);

        expect(store->getLastRotated() == lastRotated);

        {
            // This kicks off another cleanup.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            expect(goodLedger(ledger, to_string(ledgerSeq++)));
        }

        while (store->rotating())
            std::this_thread::sleep_for(1ms);

        validationCheck(env, 0);
        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);

        expect(store->getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;

        // This does not kick off a cleanup
        canDelete = env.rpc("can_delete", "now");
        expect(!RPC::contains_error(canDelete[jss::result]));
        expect(canDelete[jss::result][jss::can_delete] == ledgerSeq - 1);

        for (; ledgerSeq < lastRotated + deleteInterval; ++ledgerSeq)
        {
            // No cleanups in this loop.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            expect(goodLedger(ledger, to_string(ledgerSeq)));
        }

        while (store->rotating())
            std::this_thread::sleep_for(1ms);

        expect(store->getLastRotated() == lastRotated);

        {
            // This kicks off another cleanup.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            expect(goodLedger(ledger, to_string(ledgerSeq++)));
        }

        while (store->rotating())
            std::this_thread::sleep_for(1ms);

        validationCheck(env, 0);
        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);

        expect(store->getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;
    }

    void run()
    {
        testClear();
        testAutomatic();
        testCanDelete();
    }
};

BEAST_DEFINE_TESTSUITE(SHAMapStore,app,ripple);

}
}
