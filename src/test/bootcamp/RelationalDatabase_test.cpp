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
    run() override
    {
        testRelationalDatabaseInit();
    }
};

BEAST_DEFINE_TESTSUITE(RelationalDatabase, bootcamp, ripple);

} // namespace test
} // namespace ripple