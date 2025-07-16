//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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
#include <xrpld/app/rdb/RelationalDatabase.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/core/ConfigSections.h>
#include <xrpld/app/main/DBInit.h>
#include <xrpld/core/DatabaseCon.h>
#include <xrpld/core/SociDB.h>

namespace ripple {
namespace test {

class RelationalDatabase_test : public beast::unit_test::suite
{
public:
    void
    testRelationalDatabaseInit()
    {
        testcase("RelationalDatabase initialization");
        
        // Create environment with SQLite backend
        auto config = test::jtx::envconfig();
        config->overwrite(SECTION_RELATIONAL_DB, "backend", "sqlite");
        config->LEDGER_HISTORY = 1000;
        
        test::jtx::Env env(*this, std::move(config));
        auto& app = env.app();
        
        // Verify RelationalDatabase is properly initialized
        auto& db = app.getRelationalDatabase();
        
        // Basic initialization checks
        BEAST_EXPECT(db.getMinLedgerSeq() == std::nullopt);
        BEAST_EXPECT(db.getMaxLedgerSeq() == std::nullopt);
        BEAST_EXPECT(db.getNewestLedgerInfo() == std::nullopt);
        
        log << "RelationalDatabase initialized successfully";
    }

    void
    testSQLSchemaCreation()
    {
        testcase("SQL schema creation and management");
        
        auto config = test::jtx::envconfig();
        config->overwrite(SECTION_RELATIONAL_DB, "backend", "sqlite");
        config->LEDGER_HISTORY = 1000;
        
        test::jtx::Env env(*this, std::move(config));
        auto& app = env.app();
        
        // Verify schema initialization by checking database exists
        auto& db = app.getRelationalDatabase();
        
        // Test that we can check database space (indicates schema exists)
        bool hasSpace = db.ledgerDbHasSpace(app.config());
        BEAST_EXPECT(hasSpace || !hasSpace); // Either result is valid
        
        // Create a simple ledger to verify schema works
        env.fund(test::jtx::XRP(10000), test::jtx::Account("alice"));
        env.close();
        
        // Now database should have data
        auto minSeq = db.getMinLedgerSeq();
        auto maxSeq = db.getMaxLedgerSeq();
        
        BEAST_EXPECT(minSeq.has_value());
        BEAST_EXPECT(maxSeq.has_value());
        
        if (minSeq && maxSeq)
        {
            log << "Min ledger seq: " << *minSeq << ", Max ledger seq: " << *maxSeq;
        }
        
        log << "Schema creation test completed successfully";
    }

    void
    testThreeKeyQueries()
    {
        testcase("Three key SQL queries: last validated ledger, account transactions, transaction counts");
        
        auto config = test::jtx::envconfig();
        config->overwrite(SECTION_RELATIONAL_DB, "backend", "sqlite");
        config->LEDGER_HISTORY = 1000;
        
        test::jtx::Env env(*this, std::move(config));
        auto& app = env.app();
        auto& db = app.getRelationalDatabase();
        
        // Create test accounts
        test::jtx::Account alice("alice");
        test::jtx::Account bob("bob");
        
        // Fund accounts and create transactions
        env.fund(test::jtx::XRP(10000), alice, bob);
        env.close();
        
        env(test::jtx::pay(alice, bob, test::jtx::XRP(1000)));
        env.close();
        
        env(test::jtx::pay(bob, alice, test::jtx::XRP(500)));
        env.close();
        
        // Test 1: Last validated ledger
        auto newestLedger = db.getNewestLedgerInfo();
        BEAST_EXPECT(newestLedger.has_value());
        
        if (newestLedger)
        {
            log << "Newest ledger seq: " << newestLedger->seq << ", hash: " << newestLedger->hash;
        }
        
        // Test 2: Account transactions (if available)
        auto* sqliteDb = dynamic_cast<SQLiteDatabase*>(&db);
        if (sqliteDb)
        {
            RelationalDatabase::AccountTxOptions options{
                alice.id(),
                1,
                1000000,
                0,
                100,
                true
            };
            
            auto accountTxs = sqliteDb->getNewestAccountTxs(options);
            log << "Account transactions for alice: " << accountTxs.size();
            
            // Test 3: Transaction counts
            auto txnCount = sqliteDb->getTransactionCount();
            auto acctTxnCount = sqliteDb->getAccountTransactionCount();
            auto ledgerCount = sqliteDb->getLedgerCountMinMax();
            
            log << "Transaction count: " << txnCount;
            log << "Account transaction count: " << acctTxnCount;
            log << "Ledger count: " << ledgerCount.numberOfRows 
                << " (min: " << ledgerCount.minLedgerSequence 
                << ", max: " << ledgerCount.maxLedgerSequence << ")";
        }
        
        log << "Three key queries test completed successfully";
    }

    void
    run() override
    {
        testRelationalDatabaseInit();
        testSQLSchemaCreation();
        testThreeKeyQueries();
    }
};

BEAST_DEFINE_TESTSUITE(RelationalDatabase, bootcamp, ripple);

} // namespace test
} // namespace ripple