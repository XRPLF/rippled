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
#include <ripple/core/Config.h>
#include <ripple/core/ConfigSections.h>
#include <test/jtx/TestSuite.h>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <fstream>
#include <iostream>

namespace ripple {
namespace detail {
std::string configContents (std::string const& dbPath,
    std::string const& validatorsFile)
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

    std::string dbPathSection =
        dbPath.empty () ? "" : "[database_path]\n" + dbPath;
    std::string valFileSection =
        validatorsFile.empty () ? "" : "[validators_file]\n" + validatorsFile;
    return boost::str (
        configContentsTemplate % dbPathSection % valFileSection);
}

/**
   Write a config file and remove when done.
 */
class ConfigGuard
{
private:
    bool rmSubDir_{false};

protected:
    using path = boost::filesystem::path;
    path subDir_;
    beast::unit_test::suite& test_;

    auto rmDir (path const& toRm)
    {
        if (is_directory (toRm) && is_empty (toRm))
            remove (toRm);
        else
            test_.log << "Expected " << toRm.string ()
                      << " to be an empty existing directory." << std::endl;
    };

public:
    ConfigGuard (beast::unit_test::suite& test, path subDir,
            bool useCounter = true)
        : subDir_ (std::move (subDir))
        , test_ (test)
    {
        using namespace boost::filesystem;
        {
            static auto subDirCounter = 0;
            if (useCounter)
                subDir_ += std::to_string(++subDirCounter);
            if (!exists (subDir_))
            {
                create_directory (subDir_);
                rmSubDir_ = true;
            }
            else if (is_directory (subDir_))
                rmSubDir_ = false;
            else
            {
                // Cannot run the test. Someone created a file where we want to
                // put our directory
                Throw<std::runtime_error> (
                    "Cannot create directory: " + subDir_.string ());
            }
        }
    }

    ~ConfigGuard ()
    {
        try
        {
            using namespace boost::filesystem;

            if (rmSubDir_)
                rmDir (subDir_);
            else
                test_.log << "Skipping rm dir: "
                          << subDir_.string () << std::endl;
        }
        catch (std::exception& e)
        {
            // if we throw here, just let it die.
            test_.log << "Error in ~ConfigGuard: " << e.what () << std::endl;
        };
    }

    path const& subdir() const
    {
        return subDir_;
    }
};

/**
   Write a rippled config file and remove when done.
 */
class RippledCfgGuard : public ConfigGuard
{
private:
    path configFile_;
    path dataDir_;

    bool rmDataDir_{false};

    Config config_;

public:
    RippledCfgGuard (beast::unit_test::suite& test,
        path subDir, path const& dbPath,
            path const& validatorsFile,
                bool useCounter = true)
        : ConfigGuard (test, std::move (subDir), useCounter)
        , dataDir_ (dbPath)
    {
        if (dbPath.empty ())
            dataDir_ = subDir_ / path (Config::databaseDirName);

        configFile_ = subDir_ / path (Config::configFileName);

        if (!exists (configFile_))
        {
            std::ofstream o (configFile_.string ());
            o << configContents (dbPath.string (), validatorsFile.string ());
        }
        else
        {
            Throw<std::runtime_error> (
                "Refusing to overwrite existing config file: " +
                    configFile_.string ());
        }

        rmDataDir_ = !exists (dataDir_);
        config_.setup (configFile_.string (), /*bQuiet*/ true,
            /* bSilent */ false, /* bStandalone */ false);
    }

    Config const& config () const
    {
        return config_;
    }

    std::string configFile() const
    {
        return configFile_.string();
    }

    bool dataDirExists () const
    {
        return boost::filesystem::is_directory (dataDir_);
    }

    bool configFileExists () const
    {
        return boost::filesystem::exists (configFile_);
    }

    ~RippledCfgGuard ()
    {
        try
        {
            using namespace boost::filesystem;
            if (!boost::filesystem::exists (configFile_))
                test_.log << "Expected " << configFile_.string ()
                          << " to be an existing file." << std::endl;
            else
                remove (configFile_);

            if (rmDataDir_)
                rmDir (dataDir_);
            else
                test_.log << "Skipping rm dir: "
                          << dataDir_.string () << std::endl;
        }
        catch (std::exception& e)
        {
            // if we throw here, just let it die.
            test_.log << "Error in ~RippledCfgGuard: "
                      << e.what () << std::endl;
        };
    }
};

std::string valFileContents ()
{
    std::string configContents (R"rippleConfig(
[validators]
n949f75evCHwgyP4fPVgaHqNHxUVN15PsJEZ3B3HnXPcPjcZAoy7
n9MD5h24qrQqiyBC8aeqqCWvpiBiYQ3jxSr91uiDvmrkyHRdYLUj
n9L81uNCaPgtUJfaHh89gmdvXKAmSt5Gdsw2g1iPWaPkAHW5Nm4C
n9KiYM9CgngLvtRCQHZwgC2gjpdaZcCcbt3VboxiNFcKuwFVujzS
n9LdgEtkmGB9E2h3K4Vp7iGUaKuq23Zr32ehxiU8FWY7xoxbWTSA

[validator_keys]
nHUhG1PgAG8H8myUENypM35JgfqXAKNQvRVVAFDRzJrny5eZN8d5
nHBu9PTL9dn2GuZtdW4U2WzBwffyX9qsQCd9CNU4Z5YG3PQfViM8
nHUPDdcdb2Y5DZAJne4c2iabFuAP3F34xZUgYQT2NH7qfkdapgnz

[validator_list_sites]
recommendedripplevalidators.com
moreripplevalidators.net

[validator_list_keys]
03E74EE14CB525AFBB9F1B7D86CD58ECC4B91452294B42AB4E78F260BD905C091D
030775A669685BD6ABCEBD80385921C7851783D991A8055FD21D2F3966C96F1B56
)rippleConfig");
    return configContents;
}

/**
   Write a validators.txt file and remove when done.
 */
class ValidatorsTxtGuard : public ConfigGuard
{
private:
    path validatorsFile_;

public:
    ValidatorsTxtGuard (beast::unit_test::suite& test,
        path subDir, path const& validatorsFileName,
            bool useCounter = true)
        : ConfigGuard (test, std::move (subDir), useCounter)
    {
        using namespace boost::filesystem;
        validatorsFile_ = current_path () / subDir_ / path (
            validatorsFileName.empty () ? Config::validatorsFileName :
            validatorsFileName);

        if (!exists (validatorsFile_))
        {
            std::ofstream o (validatorsFile_.string ());
            o << valFileContents ();
        }
        else
        {
            Throw<std::runtime_error> (
                "Refusing to overwrite existing config file: " +
                    validatorsFile_.string ());
        }
    }

    bool validatorsFileExists () const
    {
        return boost::filesystem::exists (validatorsFile_);
    }

    std::string validatorsFile () const
    {
        return validatorsFile_.string ();
    }

    ~ValidatorsTxtGuard ()
    {
        try
        {
            using namespace boost::filesystem;
            if (!boost::filesystem::exists (validatorsFile_))
                test_.log << "Expected " << validatorsFile_.string ()
                          << " to be an existing file." << std::endl;
            else
                remove (validatorsFile_);
        }
        catch (std::exception& e)
        {
            // if we throw here, just let it die.
            test_.log << "Error in ~ValidatorsTxtGuard: "
                      << e.what () << std::endl;
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
)rippleConfig");

        c.loadFromString (toLoad);

        BEAST_EXPECT(c.legacy ("ssl_verify") == "0");
        expectException ([&c] {c.legacy ("server");});  // not a single line

        // set a legacy value
        BEAST_EXPECT(c.legacy ("not_in_file") == "");
        c.legacy ("not_in_file", "new_value");
        BEAST_EXPECT(c.legacy ("not_in_file") == "new_value");
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
                BEAST_EXPECT(c.legacy ("database_path") == dataDirAbs.string ());
            }
            {
                // Rel paths should convert to abs paths
                Config c;
                c.loadFromString (boost::str (cc % dataDirRel.string ()));
                BEAST_EXPECT(c.legacy ("database_path") == dataDirAbs.string ());
            }
            {
                // No db section.
                // N.B. Config::setup will give database_path a default,
                // load will not.
                Config c;
                c.loadFromString ("");
                BEAST_EXPECT(c.legacy ("database_path") == "");
            }
        }
        {
            // read from file absolute path
            auto const cwd = current_path ();
            detail::ConfigGuard const g0(*this, "test_db");
            path const dataDirRel ("test_data_dir");
            path const dataDirAbs(cwd / g0.subdir () / dataDirRel);
            detail::RippledCfgGuard const g (*this, g0.subdir(),
                dataDirAbs, "", false);
            auto const& c (g.config ());
            BEAST_EXPECT(g.dataDirExists ());
            BEAST_EXPECT(g.configFileExists ());
            BEAST_EXPECT(c.legacy ("database_path") == dataDirAbs.string ());
        }
        {
            // read from file relative path
            std::string const dbPath ("my_db");
            detail::RippledCfgGuard const g (*this, "test_db", dbPath, "");
            auto const& c (g.config ());
            std::string const nativeDbPath = absolute (path (dbPath)).string ();
            BEAST_EXPECT(g.dataDirExists ());
            BEAST_EXPECT(g.configFileExists ());
            BEAST_EXPECT(c.legacy ("database_path") == nativeDbPath);
        }
        {
            // read from file no path
            detail::RippledCfgGuard const g (*this, "test_db", "", "");
            auto const& c (g.config ());
            std::string const nativeDbPath =
                absolute (g.subdir () /
                          path (Config::databaseDirName))
                    .string ();
            BEAST_EXPECT(g.dataDirExists ());
            BEAST_EXPECT(g.configFileExists ());
            BEAST_EXPECT(c.legacy ("database_path") == nativeDbPath);
        }
    }

    void testValidatorKeys ()
    {
        testcase ("validator keys");

        std::string const validationSeed = "spA4sh1qTvwq92X715tYyGQKmAKfa";

        auto const token =
            "eyJ2YWxpZGF0aW9uX3ByaXZhdGVfa2V5IjoiOWVkNDVmODY2MjQxY2MxOGEyNzQ3Yj"
            "U0Mzg3YzA2MjU5MDc5NzJmNGU3MTkwMjMxZmFhOTM3NDU3ZmE5ZGFmNiIsIm1hbmlm"
            "ZXN0IjoiSkFBQUFBRnhJZTFGdHdtaW12R3RIMmlDY01KcUM5Z1ZGS2lsR2Z3MS92Q3"
            "hIWFhMcGxjMkduTWhBa0UxYWdxWHhCd0R3RGJJRDZPTVNZdU0wRkRBbHBBZ05rOFNL"
            "Rm43TU8yZmRrY3dSUUloQU9uZ3U5c0FLcVhZb3VKK2wyVjBXK3NBT2tWQitaUlM2UF"
            "NobEpBZlVzWGZBaUJzVkpHZXNhYWRPSmMvYUFab2tTMXZ5bUdtVnJsSFBLV1gzWXl3"
            "dTZpbjhIQVNRS1B1Z0JENjdrTWFSRkd2bXBBVEhsR0tKZHZERmxXUFl5NUFxRGVkRn"
            "Y1VEphMncwaTIxZXEzTVl5d0xWSlpuRk9yN0Mwa3cyQWlUelNDakl6ZGl0UTg9In0=";

        {
            Config c;
            static boost::format configTemplate (R"rippleConfig(
[validation_seed]
%1%

[validator_token]
%2%
)rippleConfig");
            std::string error;
            auto const expectedError =
                "Cannot have both [validation_seed] "
                "and [validator_token] config sections";
            try {
                c.loadFromString (boost::str (
                    configTemplate % validationSeed % token));
            } catch (std::runtime_error& e) {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);
        }
    }

    void testValidatorsFile ()
    {
        testcase ("validators_file");

        using namespace boost::filesystem;
        {
            // load should throw for missing specified validators file
            boost::format cc ("[validators_file]\n%1%\n");
            std::string error;
            std::string const missingPath = "/no/way/this/path/exists";
            auto const expectedError =
                "The file specified in [validators_file] does not exist: " +
                missingPath;
            try {
                Config c;
                c.loadFromString (boost::str (cc % missingPath));
            } catch (std::runtime_error& e) {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);
        }
        {
            // load should throw for invalid [validators_file]
            detail::ValidatorsTxtGuard const vtg (
                *this, "test_cfg", "validators.cfg");
            path const invalidFile = current_path () / vtg.subdir ();
            boost::format cc ("[validators_file]\n%1%\n");
            std::string error;
            auto const expectedError =
                "Invalid file specified in [validators_file]: " +
                invalidFile.string ();
            try {
                Config c;
                c.loadFromString (boost::str (cc % invalidFile.string ()));
            } catch (std::runtime_error& e) {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);
        }
        {
            // load validators from config into single section
            Config c;
            std::string toLoad(R"rippleConfig(
[validators]
n949f75evCHwgyP4fPVgaHqNHxUVN15PsJEZ3B3HnXPcPjcZAoy7
n9MD5h24qrQqiyBC8aeqqCWvpiBiYQ3jxSr91uiDvmrkyHRdYLUj
n9L81uNCaPgtUJfaHh89gmdvXKAmSt5Gdsw2g1iPWaPkAHW5Nm4C

[validator_keys]
nHUhG1PgAG8H8myUENypM35JgfqXAKNQvRVVAFDRzJrny5eZN8d5
nHBu9PTL9dn2GuZtdW4U2WzBwffyX9qsQCd9CNU4Z5YG3PQfViM8
)rippleConfig");
            c.loadFromString (toLoad);
            BEAST_EXPECT(c.legacy ("validators_file").empty ());
            BEAST_EXPECT(c.section (SECTION_VALIDATORS).values ().size () == 5);
        }
        {
            // load validator list sites and keys from config
            Config c;
            std::string toLoad(R"rippleConfig(
[validator_list_sites]
ripplevalidators.com
trustthesevalidators.gov

[validator_list_keys]
021A99A537FDEBC34E4FCA03B39BEADD04299BB19E85097EC92B15A3518801E566
)rippleConfig");
            c.loadFromString (toLoad);
            BEAST_EXPECT(
                c.section (SECTION_VALIDATOR_LIST_SITES).values ().size () == 2);
            BEAST_EXPECT(
                c.section (SECTION_VALIDATOR_LIST_SITES).values ()[0] ==
                    "ripplevalidators.com");
            BEAST_EXPECT(
                c.section (SECTION_VALIDATOR_LIST_SITES).values ()[1] ==
                    "trustthesevalidators.gov");
            BEAST_EXPECT(
                c.section (SECTION_VALIDATOR_LIST_KEYS).values ().size () == 1);
            BEAST_EXPECT(
                c.section (SECTION_VALIDATOR_LIST_KEYS).values ()[0] ==
                    "021A99A537FDEBC34E4FCA03B39BEADD04299BB19E85097EC92B15A3518801E566");
        }
        {
            // load should throw if [validator_list_sites] is configured but
            // [validator_list_keys] is not
            Config c;
            std::string toLoad(R"rippleConfig(
[validator_list_sites]
ripplevalidators.com
trustthesevalidators.gov
)rippleConfig");
            std::string error;
            auto const expectedError =
                "[validator_list_keys] config section is missing";
            try {
                c.loadFromString (toLoad);
            } catch (std::runtime_error& e) {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);
        }
        {
            // load from specified [validators_file] absolute path
            detail::ValidatorsTxtGuard const vtg (
                *this, "test_cfg", "validators.cfg");
            BEAST_EXPECT(vtg.validatorsFileExists ());
            Config c;
            boost::format cc ("[validators_file]\n%1%\n");
            c.loadFromString (boost::str (cc % vtg.validatorsFile ()));
            BEAST_EXPECT(c.legacy ("validators_file") == vtg.validatorsFile ());
            BEAST_EXPECT(c.section (SECTION_VALIDATORS).values ().size () == 8);
            BEAST_EXPECT(
                c.section (SECTION_VALIDATOR_LIST_SITES).values ().size () == 2);
            BEAST_EXPECT(
                c.section (SECTION_VALIDATOR_LIST_KEYS).values ().size () == 2);
        }
        {
            // load from specified [validators_file] file name
            // in config directory
            std::string const valFileName = "validators.txt";
            detail::ValidatorsTxtGuard const vtg (
                *this, "test_cfg", valFileName);
            detail::RippledCfgGuard const rcg (
                *this, vtg.subdir (), "", valFileName, false);
            BEAST_EXPECT(vtg.validatorsFileExists ());
            BEAST_EXPECT(rcg.configFileExists ());
            auto const& c (rcg.config ());
            BEAST_EXPECT(c.legacy ("validators_file") == valFileName);
            BEAST_EXPECT(c.section (SECTION_VALIDATORS).values ().size () == 8);
            BEAST_EXPECT(
                c.section (SECTION_VALIDATOR_LIST_SITES).values ().size () == 2);
            BEAST_EXPECT(
                c.section (SECTION_VALIDATOR_LIST_KEYS).values ().size () == 2);
        }
        {
            // load from specified [validators_file] relative path
            // to config directory
            detail::ValidatorsTxtGuard const vtg (
                *this, "test_cfg", "validators.txt");
            auto const valFilePath = ".." / vtg.subdir() / "validators.txt";
            detail::RippledCfgGuard const rcg (
                *this, vtg.subdir (), "", valFilePath, false);
            BEAST_EXPECT(vtg.validatorsFileExists ());
            BEAST_EXPECT(rcg.configFileExists ());
            auto const& c (rcg.config ());
            BEAST_EXPECT(c.legacy ("validators_file") == valFilePath);
            BEAST_EXPECT(c.section (SECTION_VALIDATORS).values ().size () == 8);
            BEAST_EXPECT(
                c.section (SECTION_VALIDATOR_LIST_SITES).values ().size () == 2);
            BEAST_EXPECT(
                c.section (SECTION_VALIDATOR_LIST_KEYS).values ().size () == 2);
        }
        {
            // load from validators file in default location
            detail::ValidatorsTxtGuard const vtg (
                *this, "test_cfg", "validators.txt");
            detail::RippledCfgGuard const rcg (*this, vtg.subdir (),
                "", "", false);
            BEAST_EXPECT(vtg.validatorsFileExists ());
            BEAST_EXPECT(rcg.configFileExists ());
            auto const& c (rcg.config ());
            BEAST_EXPECT(c.legacy ("validators_file").empty ());
            BEAST_EXPECT(c.section (SECTION_VALIDATORS).values ().size () == 8);
            BEAST_EXPECT(
                c.section (SECTION_VALIDATOR_LIST_SITES).values ().size () == 2);
            BEAST_EXPECT(
                c.section (SECTION_VALIDATOR_LIST_KEYS).values ().size () == 2);
        }
        {
            // load from specified [validators_file] instead
            // of default location
            detail::ValidatorsTxtGuard const vtg (
                *this, "test_cfg", "validators.cfg");
            BEAST_EXPECT(vtg.validatorsFileExists ());
            detail::ValidatorsTxtGuard const vtgDefault (
                *this, vtg.subdir (), "validators.txt", false);
            BEAST_EXPECT(vtgDefault.validatorsFileExists ());
            detail::RippledCfgGuard const rcg (
                *this, vtg.subdir (), "", vtg.validatorsFile (), false);
            BEAST_EXPECT(rcg.configFileExists ());
            auto const& c (rcg.config ());
            BEAST_EXPECT(c.legacy ("validators_file") == vtg.validatorsFile ());
            BEAST_EXPECT(c.section (SECTION_VALIDATORS).values ().size () == 8);
            BEAST_EXPECT(
                c.section (SECTION_VALIDATOR_LIST_SITES).values ().size () == 2);
            BEAST_EXPECT(
                c.section (SECTION_VALIDATOR_LIST_KEYS).values ().size () == 2);
        }

        {
            // load validators from both config and validators file
            boost::format cc (R"rippleConfig(
[validators_file]
%1%

[validators]
n949f75evCHwgyP4fPVgaHqNHxUVN15PsJEZ3B3HnXPcPjcZAoy7
n9MD5h24qrQqiyBC8aeqqCWvpiBiYQ3jxSr91uiDvmrkyHRdYLUj
n9L81uNCaPgtUJfaHh89gmdvXKAmSt5Gdsw2g1iPWaPkAHW5Nm4C
n9KiYM9CgngLvtRCQHZwgC2gjpdaZcCcbt3VboxiNFcKuwFVujzS
n9LdgEtkmGB9E2h3K4Vp7iGUaKuq23Zr32ehxiU8FWY7xoxbWTSA

[validator_keys]
nHB1X37qrniVugfQcuBTAjswphC1drx7QjFFojJPZwKHHnt8kU7v
nHUkAWDR4cB8AgPg7VXMX6et8xRTQb2KJfgv1aBEXozwrawRKgMB

[validator_list_sites]
ripplevalidators.com
trustthesevalidators.gov

[validator_list_keys]
021A99A537FDEBC34E4FCA03B39BEADD04299BB19E85097EC92B15A3518801E566
)rippleConfig");
            detail::ValidatorsTxtGuard const vtg (
                *this, "test_cfg", "validators.cfg");
            BEAST_EXPECT(vtg.validatorsFileExists ());
            Config c;
            c.loadFromString (boost::str (cc % vtg.validatorsFile ()));
            BEAST_EXPECT(c.legacy ("validators_file") == vtg.validatorsFile ());
            BEAST_EXPECT(c.section (SECTION_VALIDATORS).values ().size () == 15);
            BEAST_EXPECT(
                c.section (SECTION_VALIDATOR_LIST_SITES).values ().size () == 4);
            BEAST_EXPECT(
                c.section (SECTION_VALIDATOR_LIST_KEYS).values ().size () == 3);
        }
        {
            // load should throw if [validators], [validator_keys] and
            // [validator_list_keys] are missing from rippled cfg and
            // validators file
            Config c;
            boost::format cc ("[validators_file]\n%1%\n");
            std::string error;
            detail::ValidatorsTxtGuard const vtg (
                *this, "test_cfg", "validators.cfg");
            BEAST_EXPECT(vtg.validatorsFileExists ());
            auto const expectedError =
                "The file specified in [validators_file] does not contain a "
                "[validators], [validator_keys] or [validator_list_keys] section: " +
                vtg.validatorsFile ();
            std::ofstream o (vtg.validatorsFile ());
            try {
                Config c;
                c.loadFromString (boost::str (cc % vtg.validatorsFile ()));
            } catch (std::runtime_error& e) {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);
        }
    }

    void testSetup(bool explicitPath)
    {
        detail::RippledCfgGuard const cfg(*this, "testSetup",
            explicitPath ? "test_db" : "", "");
        /* ConfigGuard has a Config object that gets loaded on construction,
            but Config::setup is not reentrant, so we need a fresh config
            for every test case, so ignore it.
        */
        {
            Config config;
            config.setup(cfg.configFile(), /*bQuiet*/ false,
                /* bSilent */ false, /* bStandalone */ false);
            BEAST_EXPECT(!config.quiet());
            BEAST_EXPECT(!config.silent());
            BEAST_EXPECT(!config.standalone());
            BEAST_EXPECT(config.LEDGER_HISTORY == 256);
            BEAST_EXPECT(!config.legacy("database_path").empty());
        }
        {
            Config config;
            config.setup(cfg.configFile(), /*bQuiet*/ true,
                /* bSilent */ false, /* bStandalone */ false);
            BEAST_EXPECT(config.quiet());
            BEAST_EXPECT(!config.silent());
            BEAST_EXPECT(!config.standalone());
            BEAST_EXPECT(config.LEDGER_HISTORY == 256);
            BEAST_EXPECT(!config.legacy("database_path").empty());
        }
        {
            Config config;
            config.setup(cfg.configFile(), /*bQuiet*/ false,
                /* bSilent */ true, /* bStandalone */ false);
            BEAST_EXPECT(config.quiet());
            BEAST_EXPECT(config.silent());
            BEAST_EXPECT(!config.standalone());
            BEAST_EXPECT(config.LEDGER_HISTORY == 256);
            BEAST_EXPECT(!config.legacy("database_path").empty());
        }
        {
            Config config;
            config.setup(cfg.configFile(), /*bQuiet*/ true,
                /* bSilent */ true, /* bStandalone */ false);
            BEAST_EXPECT(config.quiet());
            BEAST_EXPECT(config.silent());
            BEAST_EXPECT(!config.standalone());
            BEAST_EXPECT(config.LEDGER_HISTORY == 256);
            BEAST_EXPECT(!config.legacy("database_path").empty());
        }
        {
            Config config;
            config.setup(cfg.configFile(), /*bQuiet*/ false,
                /* bSilent */ false, /* bStandalone */ true);
            BEAST_EXPECT(!config.quiet());
            BEAST_EXPECT(!config.silent());
            BEAST_EXPECT(config.standalone());
            BEAST_EXPECT(config.LEDGER_HISTORY == 0);
            BEAST_EXPECT(config.legacy("database_path").empty() == !explicitPath);
        }
        {
            Config config;
            config.setup(cfg.configFile(), /*bQuiet*/ true,
                /* bSilent */ false, /* bStandalone */ true);
            BEAST_EXPECT(config.quiet());
            BEAST_EXPECT(!config.silent());
            BEAST_EXPECT(config.standalone());
            BEAST_EXPECT(config.LEDGER_HISTORY == 0);
            BEAST_EXPECT(config.legacy("database_path").empty() == !explicitPath);
        }
        {
            Config config;
            config.setup(cfg.configFile(), /*bQuiet*/ false,
                /* bSilent */ true, /* bStandalone */ true);
            BEAST_EXPECT(config.quiet());
            BEAST_EXPECT(config.silent());
            BEAST_EXPECT(config.standalone());
            BEAST_EXPECT(config.LEDGER_HISTORY == 0);
            BEAST_EXPECT(config.legacy("database_path").empty() == !explicitPath);
        }
        {
            Config config;
            config.setup(cfg.configFile(), /*bQuiet*/ true,
                /* bSilent */ true, /* bStandalone */ true);
            BEAST_EXPECT(config.quiet());
            BEAST_EXPECT(config.silent());
            BEAST_EXPECT(config.standalone());
            BEAST_EXPECT(config.LEDGER_HISTORY == 0);
            BEAST_EXPECT(config.legacy("database_path").empty() == !explicitPath);
        }
    }

    void run ()
    {
        testLegacy ();
        testDbPath ();
        testValidatorKeys ();
        testValidatorsFile ();
        testSetup (false);
        testSetup (true);
    }
};

BEAST_DEFINE_TESTSUITE (Config, core, ripple);

}  // ripple
