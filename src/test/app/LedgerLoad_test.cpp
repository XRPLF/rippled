//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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

#include <ripple/beast/unit_test.h>
#include <test/jtx.h>
#include <test/jtx/Env.h>
#include <ripple/beast/utility/temp_dir.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/SField.h>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <fstream>

namespace ripple {

class LedgerLoad_test : public beast::unit_test::suite
{
    auto static ledgerConfig(
        std::unique_ptr<Config> cfg,
        std::string const& dbPath,
        std::string const& ledger,
        Config::StartUpType type)
    {
        cfg->START_LEDGER = ledger;
        cfg->START_UP = type;
        assert(! dbPath.empty());
        cfg->legacy("database_path", dbPath);
        return cfg;
    }

    // setup for test cases
    struct SetupData
    {
        std::string const dbPath;
        std::string ledgerFile;
        Json::Value ledger;
        Json::Value hashes;
    };

    SetupData
    setupLedger(beast::temp_dir const& td)
    {
        using namespace test::jtx;
        SetupData retval = {td.path()};

        retval.ledgerFile = td.file("ledgerdata.json");

        Env env {*this};
        Account prev;

        for(auto i = 0; i < 20; ++i)
        {
            Account acct {"A" + std::to_string(i)};
            env.fund(XRP(10000), acct);
            env.close();
            if(i > 0)
            {
                env.trust(acct["USD"](1000), prev);
                env(pay(acct, prev, acct["USD"](5)));
            }
            env(offer(acct, XRP(100), acct["USD"](1)));
            env.close();
            prev = std::move(acct);
        }

        retval.ledger = env.rpc ("ledger", "current", "full") [jss::result];
        BEAST_EXPECT(retval.ledger[jss::ledger][jss::accountState].size() == 101);

        retval.hashes = [&] {
            for(auto const& it : retval.ledger[jss::ledger][jss::accountState])
            {
                if(it[sfLedgerEntryType.fieldName] == "LedgerHashes")
                    return it[sfHashes.fieldName];
            }
            return Json::Value {};
        }();

        BEAST_EXPECT(retval.hashes.size() == 41);

        //write this ledger data to a file.
        std::ofstream o (retval.ledgerFile, std::ios::out | std::ios::trunc);
        o << to_string(retval.ledger);
        o.close();
        return retval;
    }

    void
    testLoad (SetupData const& sd)
    {
        testcase ("Load a saved ledger");
        using namespace test::jtx;

        // create a new env with the ledger file specified for startup
        Env env(*this,
            envconfig( ledgerConfig,
                sd.dbPath, sd.ledgerFile, Config::LOAD_FILE));
        auto jrb = env.rpc ( "ledger", "current", "full") [jss::result];
        BEAST_EXPECT(
            sd.ledger[jss::ledger][jss::accountState].size() ==
            jrb[jss::ledger][jss::accountState].size());
    }

    void
    testBadFiles (SetupData const& sd)
    {
        testcase ("Load ledger: Bad Files");
        using namespace test::jtx;
        using namespace boost::filesystem;

        // empty path
        except ([&]
        {
            Env env(*this,
                envconfig( ledgerConfig,
                    sd.dbPath, "", Config::LOAD_FILE));
        });

        // file does not exist
        except ([&]
        {
            Env env(*this,
                envconfig( ledgerConfig,
                    sd.dbPath, "badfile.json", Config::LOAD_FILE));
        });

        // make a corrupted version of the ledger file (last 10 bytes removed).
        boost::system::error_code ec;
        auto ledgerFileCorrupt =
            boost::filesystem::path{sd.dbPath} / "ledgerdata_bad.json";
        copy_file(
            sd.ledgerFile,
            ledgerFileCorrupt,
            copy_option::overwrite_if_exists,
            ec);
        if(! BEAST_EXPECTS(!ec, ec.message()))
            return;
        auto filesize = file_size(ledgerFileCorrupt, ec);
        if(! BEAST_EXPECTS(!ec, ec.message()))
            return;
        resize_file(ledgerFileCorrupt, filesize - 10, ec);
        if(! BEAST_EXPECTS(!ec, ec.message()))
            return;

        except ([&]
        {
            Env env(*this,
                envconfig( ledgerConfig,
                    sd.dbPath, ledgerFileCorrupt.string(), Config::LOAD_FILE));
        });
    }

    void
    testLoadByHash (SetupData const& sd)
    {
        testcase ("Load by hash");
        using namespace test::jtx;

        // create a new env with the ledger hash specified for startup
        auto ledgerHash = to_string(sd.hashes[sd.hashes.size()-1]);
        boost::erase_all(ledgerHash, "\"");
        Env env(*this,
            envconfig( ledgerConfig,
                sd.dbPath, ledgerHash, Config::LOAD));
        auto jrb = env.rpc ( "ledger", "current", "full") [jss::result];
        BEAST_EXPECT(jrb[jss::ledger][jss::accountState].size() == 97);
        BEAST_EXPECT(
            jrb[jss::ledger][jss::accountState].size() <=
            sd.ledger[jss::ledger][jss::accountState].size());
    }

    void
    testLoadLatest (SetupData const& sd)
    {
        testcase ("Load by keyword");
        using namespace test::jtx;

        // create a new env with the ledger "latest" specified for startup
        Env env(*this,
            envconfig( ledgerConfig,
                sd.dbPath, "latest", Config::LOAD));
        auto jrb = env.rpc ( "ledger", "current", "full") [jss::result];
        BEAST_EXPECT(
            sd.ledger[jss::ledger][jss::accountState].size() ==
            jrb[jss::ledger][jss::accountState].size());
    }

    void
    testLoadIndex (SetupData const& sd)
    {
        testcase ("Load by index");
        using namespace test::jtx;

        // create a new env with specific ledger index at startup
        Env env(*this,
            envconfig( ledgerConfig,
                sd.dbPath, "43", Config::LOAD));
        auto jrb = env.rpc ( "ledger", "current", "full") [jss::result];
        BEAST_EXPECT(
            sd.ledger[jss::ledger][jss::accountState].size() ==
            jrb[jss::ledger][jss::accountState].size());
    }

public:
    void run () override
    {
        beast::temp_dir td;
        auto sd = setupLedger(td);

        // test cases
        testLoad (sd);
        testBadFiles (sd);
        testLoadByHash (sd);
        testLoadLatest (sd);
        testLoadIndex (sd);
    }
};

BEAST_DEFINE_TESTSUITE (LedgerLoad, app, ripple);

}  // ripple
