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

#include <BeastConfig.h>
#include <test/jtx.h>
#include <test/jtx/Env.h>
#include <ripple/beast/unit_test.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/SField.h>
#include <boost/scope_exit.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <fstream>

namespace ripple {

class LedgerLoad_test : public beast::unit_test::suite
{
    Json::Value jvLedger_;
    Json::Value jvHashes_;
    boost::filesystem::path ledgerFile_;
    boost::filesystem::path dbPath_;

    auto ledgerConfig(std::string const& ledger, Config::StartUpType type)
    {
        assert(! dbPath_.empty());
        auto p = test::jtx::envconfig();
        p->START_LEDGER = ledger;
        p->START_UP = type;
        p->legacy("database_path", dbPath_.string());
        return p;
    }

    // setup for test cases
    void
    setupLedger()
    {
        using namespace test::jtx;

        boost::system::error_code ec;
        // create a temporary path to write ledger files in
        dbPath_  = boost::filesystem::temp_directory_path(ec);
        if(! BEAST_EXPECTS(!ec, ec.message()))
            return;
        dbPath_ /= boost::filesystem::unique_path("%%%%-%%%%-%%%%-%%%%", ec);
        if(! BEAST_EXPECTS(!ec, ec.message()))
            return;
        boost::filesystem::create_directories(dbPath_, ec);
        if(! BEAST_EXPECTS(!ec, ec.message()))
            return;

        ledgerFile_ = dbPath_ / "ledgerdata.json";

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

        jvLedger_ = env.rpc ("ledger", "current", "full") [jss::result];
        BEAST_EXPECT(jvLedger_[jss::ledger][jss::accountState].size() == 101);

        for(auto const& it : jvLedger_[jss::ledger][jss::accountState])
        {
            if(it[sfLedgerEntryType.fieldName] == "LedgerHashes")
            {
                jvHashes_ = it[sfHashes.fieldName];
            }
        }
        BEAST_EXPECT(jvHashes_.size() == 41);

        //write this ledger data to a file.
        std::ofstream o (ledgerFile_.string(), std::ios::out | std::ios::trunc);
        o << to_string(jvLedger_);
        o.close();
    }

    void
    testLoad ()
    {
        testcase ("Load a saved ledger");
        using namespace test::jtx;

        // create a new env with the ledger file specified for startup
        Env env(*this, ledgerConfig(ledgerFile_.string(), Config::LOAD_FILE));
        auto jrb = env.rpc ( "ledger", "current", "full") [jss::result];
        BEAST_EXPECT(
            jvLedger_[jss::ledger][jss::accountState].size() ==
            jrb[jss::ledger][jss::accountState].size());
    }

    void
    testBadFiles ()
    {
        testcase ("Load ledger: Bad Files");
        using namespace test::jtx;
        using namespace boost::filesystem;

        // empty path
        except ([this]
        {
            Env env(*this, ledgerConfig("", Config::LOAD_FILE));
        });

        // file does not exist
        except ([this]
        {
            Env env(*this, ledgerConfig("badfile.json", Config::LOAD_FILE));
        });

        // make a corrupted version of the ledger file (last 10 bytes removed).
        boost::system::error_code ec;
        auto ledgerFileCorrupt = dbPath_ / "ledgerdata_bad.json";
        copy_file(
            ledgerFile_,
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

        except ([this, &ledgerFileCorrupt]
        {
            Env env(*this, ledgerConfig(ledgerFileCorrupt.string(), Config::LOAD_FILE));
        });
    }

    void
    testLoadByHash ()
    {
        testcase ("Load by hash");
        using namespace test::jtx;

        // create a new env with the ledger hash specified for startup
        auto ledgerHash = to_string(jvHashes_[jvHashes_.size()-1]);
        boost::erase_all(ledgerHash, "\"");
        Env env(*this, ledgerConfig(ledgerHash, Config::LOAD));
        auto jrb = env.rpc ( "ledger", "current", "full") [jss::result];
        BEAST_EXPECT(jrb[jss::ledger][jss::accountState].size() == 97);
        BEAST_EXPECT(
            jrb[jss::ledger][jss::accountState].size() <=
            jvLedger_[jss::ledger][jss::accountState].size());
    }

    void
    testLoadLatest ()
    {
        testcase ("Load by keyword");
        using namespace test::jtx;

        // create a new env with the ledger "latest" specified for startup
        Env env(*this, ledgerConfig("latest", Config::LOAD));
        auto jrb = env.rpc ( "ledger", "current", "full") [jss::result];
        BEAST_EXPECT(
            jvLedger_[jss::ledger][jss::accountState].size() ==
            jrb[jss::ledger][jss::accountState].size());
    }

    void
    testLoadIndex ()
    {
        testcase ("Load by index");
        using namespace test::jtx;

        // create a new env with specific ledger index at startup
        Env env(*this, ledgerConfig("43", Config::LOAD));
        auto jrb = env.rpc ( "ledger", "current", "full") [jss::result];
        BEAST_EXPECT(
            jvLedger_[jss::ledger][jss::accountState].size() ==
            jrb[jss::ledger][jss::accountState].size());
    }

public:
    void run ()
    {
        setupLedger();
        BOOST_SCOPE_EXIT( this_ ) {
            boost::system::error_code ec;
            boost::filesystem::remove_all(this_->dbPath_, ec);
            this_->expect(!ec, ec.message(), __FILE__, __LINE__);
        } BOOST_SCOPE_EXIT_END

        // test cases
        testLoad ();
        testBadFiles ();
        testLoadByHash ();
        testLoadLatest ();
        testLoadIndex ();
    }
};

BEAST_DEFINE_TESTSUITE (LedgerLoad, app, ripple);

}  // ripple
