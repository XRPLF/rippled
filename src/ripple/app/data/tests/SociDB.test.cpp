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

#include <ripple/app/data/SociDB.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/basics/TestSuite.h>
#include <ripple/basics/BasicConfig.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>

namespace ripple {
class SociDB_test final : public TestSuite
{
private:
    static void setupSQLiteConfig (BasicConfig& config,
                                   boost::filesystem::path const& dbPath)
    {
        config.overwrite ("sqdb", "backend", "sqlite");
        auto value = dbPath.string ();
        if (!value.empty ())
            config.legacy ("database_path", value);
    }

    static void setupPostgresqlConfig (BasicConfig& config,
                                       std::string const& host,
                                       std::string const& user,
                                       std::string const& port)
    {
        config.overwrite ("sqdb", "backend", "postgresql");
        if (!host.empty ())
            config.overwrite ("sqdb", "host", host);
        if (!user.empty ())
            config.overwrite ("sqdb", "user", user);
        if (!port.empty ())
            config.overwrite ("sqdb", "port", port);
    }

    static void cleanupDatabaseDir (boost::filesystem::path const& dbPath)
    {
        using namespace boost::filesystem;
        if (!exists (dbPath) || !is_directory (dbPath) || !is_empty (dbPath))
            return;
        remove (dbPath);
    }

    static void setupDatabaseDir (boost::filesystem::path const& dbPath)
    {
        using namespace boost::filesystem;
        if (!exists (dbPath))
        {
            create_directory (dbPath);
            return;
        }

        if (!is_directory (dbPath))
        {
            // someone created a file where we want to put out directory
            throw std::runtime_error ("Cannot create directory: " +
                                      dbPath.string ());
        }
    }
    static boost::filesystem::path getDatabasePath ()
    {
        return boost::filesystem::current_path () / "socidb_test_databases";
    }

public:
    SociDB_test ()
    {
        try
        {
            setupDatabaseDir (getDatabasePath ());
        }
        catch (...)
        {
        }
    }
    ~SociDB_test ()
    {
        try
        {
            cleanupDatabaseDir (getDatabasePath ());
        }
        catch (...)
        {
        }
    }
    void testSQLiteFileNames ()
    {
        // confirm that files are given the correct exensions
        testcase ("sqliteFileNames");
        BasicConfig c;
        setupSQLiteConfig (c, getDatabasePath ());
        std::vector<std::pair<const char*, const char*>> const d (
            {{"peerfinder", ".sqlite"},
             {"state", ".db"},
             {"random", ".db"},
             {"validators", ".sqlite"}});

        for (auto const& i : d)
        {
            SociConfig sc (c, i.first);
            expect (boost::ends_with (sc.connectionString (),
                                      std::string (i.first) + i.second));
        }
    }
    void testSQLiteSession ()
    {
        testcase ("open");
        BasicConfig c;
        setupSQLiteConfig (c, getDatabasePath ());
        SociConfig sc (c, "SociTestDB");
        std::vector<std::string> const stringData (
            {"String1", "String2", "String3"});
        std::vector<int> const intData ({1, 2, 3});
        auto checkValues = [this, &stringData, &intData](soci::session& s)
        {
            // Check values in db
            std::vector<std::string> stringResult (20 * stringData.size ());
            std::vector<int> intResult (20 * intData.size ());
            s << "SELECT StringData, IntData FROM SociTestTable",
                soci::into (stringResult), soci::into (intResult);
            expect (stringResult.size () == stringData.size () &&
                    intResult.size () == intData.size ());
            for (int i = 0; i < stringResult.size (); ++i)
            {
                auto si = std::distance (stringData.begin (),
                                         std::find (stringData.begin (),
                                                    stringData.end (),
                                                    stringResult[i]));
                auto ii = std::distance (
                    intData.begin (),
                    std::find (intData.begin (), intData.end (), intResult[i]));
                expect (si == ii && si < stringResult.size ());
            }
        };

        {
            soci::session s;
            sc.open (s);
            s << "CREATE TABLE IF NOT EXISTS SociTestTable ("
                 "  Key                    INTEGER PRIMARY KEY,"
                 "  StringData             TEXT,"
                 "  IntData                INTEGER"
                 ");";

            s << "INSERT INTO SociTestTable (StringData, IntData) VALUES "
                 "(:stringData, :intData);",
                soci::use (stringData), soci::use (intData);
            checkValues (s);
        }
        {
            // Check values in db after session was closed
            soci::session s;
            sc.open (s);
            checkValues (s);
        }
        {
            using namespace boost::filesystem;
            // Remove the database
            path dbPath (sc.connectionString ());
            if (is_regular_file (dbPath))
                remove (dbPath);
        }
    }
    void testSQLite ()
    {
        testSQLiteFileNames ();
        testSQLiteSession ();
    }
    void run ()
    {
        testSQLite ();
    }
};

BEAST_DEFINE_TESTSUITE (SociDB, app, ripple);

}  // ripple
