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
#include <ripple/basics/contract.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/core/Config.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/basics/TestSuite.h>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <fstream>
#include <iostream>

namespace ripple {
namespace detail {
std::string configContents (std::string const& dbPath)
{
    static boost::format configContentsTemplate (R"rippleConfig(
[server]
port_rpc
port_peer
port_wss_admin

[port_rpc]
port = 5005
ip = 127.0.0.1
admin = 127.0.0.1
protocol = https

[port_peer]
port = 51235
ip = 0.0.0.0
protocol = peer

[port_wss_admin]
port = 6006
ip = 127.0.0.1
admin = 127.0.0.1
protocol = wss

#[port_ws_public]
#port = 5005
#ip = 127.0.0.1
#protocol = wss

#-------------------------------------------------------------------------------

[node_size]
medium

# This is primary persistent datastore for rippled.  This includes transaction
# metadata, account states, and ledger headers.  Helpful information can be
# found here: https://ripple.com/wiki/NodeBackEnd
# delete old ledgers while maintaining at least 2000. Do not require an
# external administrative command to initiate deletion.
[node_db]
type=memory
path=/Users/dummy/ripple/config/db/rocksdb
open_files=2000
filter_bits=12
cache_mb=256
file_size_mb=8
file_size_mult=2

%1%
%2%

# This needs to be an absolute directory reference, not a relative one.
# Modify this value as required.
[debug_logfile]
/Users/dummy/ripple/config/log/debug.log

[sntp_servers]
time.windows.com
time.apple.com
time.nist.gov
pool.ntp.org

# Where to find some other servers speaking the Ripple protocol.
#
[ips]
r.ripple.com 51235

# The latest validators can be obtained from
# https://ripple.com/ripple.txt
#
[validators]
n949f75evCHwgyP4fPVgaHqNHxUVN15PsJEZ3B3HnXPcPjcZAoy7    RL1
n9MD5h24qrQqiyBC8aeqqCWvpiBiYQ3jxSr91uiDvmrkyHRdYLUj    RL2
n9L81uNCaPgtUJfaHh89gmdvXKAmSt5Gdsw2g1iPWaPkAHW5Nm4C    RL3
n9KiYM9CgngLvtRCQHZwgC2gjpdaZcCcbt3VboxiNFcKuwFVujzS    RL4
n9LdgEtkmGB9E2h3K4Vp7iGUaKuq23Zr32ehxiU8FWY7xoxbWTSA    RL5

# Ditto.
[validation_quorum]
3

# Turn down default logging to save disk space in the long run.
# Valid values here are trace, debug, info, warning, error, and fatal
[rpc_startup]
{ "command": "log_level", "severity": "warning" }

# Defaults to 1 ("yes") so that certificates will be validated. To allow the use
# of self-signed certificates for development or internal use, set to 0 ("no").
[ssl_verify]
0

[sqdb]
backend=sqlite
)rippleConfig");

    if (!dbPath.empty ())
        return boost::str (configContentsTemplate % "[database_path]" % dbPath);
    else
        return boost::str (configContentsTemplate % "" % "");
}

/**
   Write a config file and remove when done.
 */
class ConfigGuard
{
private:
    using path = boost::filesystem::path;
    path subDir_;
    path configFile_;
    path dataDir_;

    bool rmSubDir_{false};
    bool rmDataDir_{false};

    Config config_;

public:
    ConfigGuard (std::string subDir, std::string const& dbPath)
        : subDir_ (std::move (subDir)), dataDir_ (dbPath)
    {
        using namespace boost::filesystem;

        if (dbPath.empty ())
        {
            dataDir_ = subDir_ / path (Config::Helpers::getDatabaseDirName ());
        }

        configFile_ = subDir_ / path (Config::Helpers::getConfigFileName ());
        {
            if (!exists (subDir_))
            {
                create_directory (subDir_);
                rmSubDir_ = true;
            }
            else if (is_directory (subDir_))
                rmSubDir_ = false;
            else
            {
                // Cannot run the test someone created a file where we want to
                // put out directory
                Throw<std::runtime_error> (
                    "Cannot create directory: " + subDir_.string ());
            }
        }

        if (!exists (configFile_))
        {
            std::ofstream o (configFile_.string ());
            o << configContents (dbPath);
        }
        else
        {
            Throw<std::runtime_error> (
                "Refusing to overwrite existing config file: " +
                    configFile_.string ());
        }

        rmDataDir_ = !exists (dataDir_);
        config_.setup (configFile_.string (), /*bQuiet*/ false);
    }
    Config& config ()
    {
        return config_;
    }
    bool dataDirExists () const
    {
        return boost::filesystem::is_directory (dataDir_);
    }
    bool configFileExists () const
    {
        return boost::filesystem::is_regular_file (configFile_);
    }
    ~ConfigGuard ()
    {
        try
        {
            using namespace boost::filesystem;
            if (!is_regular_file (configFile_))
                std::cerr << "Expected " << configFile_.string ()
                          << " to be an existing file.\n";
            else
                remove (configFile_.string ());

            auto rmDir = [](path const& toRm)
            {
                if (is_directory (toRm) && is_empty (toRm))
                    remove (toRm);
                else
                    std::cerr << "Expected " << toRm.string ()
                              << " to be an empty existing directory.\n";
            };

            if (rmDataDir_)
                rmDir (dataDir_);
            else
                std::cerr << "Skipping rm dir: " << dataDir_.string () << "\n";

            if (rmSubDir_)
                rmDir (subDir_);
            else
                std::cerr << "Skipping rm dir: " << subDir_.string () << "\n";
        }
        catch (std::exception& e)
        {
            // if we throw here, just let it die.
            std::cerr << "Error in CreateConfigGuard: " << e.what () << "\n";
        };
    }
};
}  // detail
class Config_test final : public TestSuite
{
private:
    using path = boost::filesystem::path;

public:
    void testLegacy ()
    {
        testcase ("legacy");

        Config c;

        std::string toLoad(R"rippleConfig(
[server]
port_rpc
port_peer
port_wss_admin

[ssl_verify]
0

[validation_quorum]
3
)rippleConfig");

        c.loadFromString (toLoad);

        expect (c.legacy ("ssl_verify") == "0");
        expectException ([&c] {c.legacy ("server");});  // not a single line

        // set a legacy value
        expect (c.legacy ("not_in_file") == "");
        c.legacy ("not_in_file", "new_value");
        expect (c.legacy ("not_in_file") == "new_value");
    }
    void testDbPath ()
    {
        testcase ("database_path");

        using namespace boost::filesystem;
        {
            boost::format cc ("[database_path]\n%1%\n");

            auto const cwd = current_path ();
            path const dataDirRel ("test_data_dir");
            path const dataDirAbs (cwd / dataDirRel);
            {
                // Dummy test - do we get back what we put in
                Config c;
                c.loadFromString (boost::str (cc % dataDirAbs.string ()));
                expect (c.legacy ("database_path") == dataDirAbs.string ());
            }
            {
                // Rel paths should convert to abs paths
                Config c;
                c.loadFromString (boost::str (cc % dataDirRel.string ()));
                expect (c.legacy ("database_path") == dataDirAbs.string ());
            }
            {
                // No db sectcion.
                // N.B. Config::setup will give database_path a default,
                // load will not.
                Config c;
                c.loadFromString ("");
                expect (c.legacy ("database_path") == "");
            }
        }
        {
            // read from file absolute path
            auto const cwd = current_path ();
            path const dataDirRel ("test_data_dir");
            path const dataDirAbs (cwd / path ("test_db") / dataDirRel);
            detail::ConfigGuard g ("test_db", dataDirAbs.string ());
            auto& c (g.config ());
            expect (g.dataDirExists ());
            expect (g.configFileExists ());
            expect (c.legacy ("database_path") == dataDirAbs.string (),
                    "dbPath Abs Path File");
        }
        {
            // read from file relative path
            std::string const dbPath ("my_db");
            detail::ConfigGuard g ("test_db", dbPath);
            auto& c (g.config ());
            std::string const nativeDbPath = absolute (path (dbPath)).string ();
            expect (g.dataDirExists ());
            expect (g.configFileExists ());
            expect (c.legacy ("database_path") == nativeDbPath,
                    "dbPath Rel Path File");
        }
        {
            // read from file no path
            detail::ConfigGuard g ("test_db", "");
            auto& c (g.config ());
            std::string const nativeDbPath =
                absolute (path ("test_db") /
                          path (Config::Helpers::getDatabaseDirName ()))
                    .string ();
            expect (g.dataDirExists ());
            expect (g.configFileExists ());
            expect (c.legacy ("database_path") == nativeDbPath,
                    "dbPath No Path");
        }
    }
    void run ()
    {
        testLegacy ();
        testDbPath ();
    }
};

BEAST_DEFINE_TESTSUITE (Config, core, ripple);

}  // ripple
