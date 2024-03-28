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

#include <ripple/basics/contract.h>
#include <ripple/core/Config.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/server/Port.h>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <fstream>
#include <iostream>
#include <test/jtx/TestSuite.h>
#include <test/unit_test/FileDirGuard.h>

namespace ripple {
namespace detail {
std::string
configContents(std::string const& dbPath, std::string const& validatorsFile)
{
    static boost::format configContentsTemplate(R"rippleConfig(
[server]
port_rpc
port_peer
port_wss_admin

[port_rpc]
port = 5005
ip = 127.0.0.1
admin = 127.0.0.1, ::1
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
# found on https://xrpl.org/capacity-planning.html#node-db-type
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
        dbPath.empty() ? "" : "[database_path]\n" + dbPath;
    std::string valFileSection =
        validatorsFile.empty() ? "" : "[validators_file]\n" + validatorsFile;
    return boost::str(configContentsTemplate % dbPathSection % valFileSection);
}

/**
   Write a rippled config file and remove when done.
 */
class RippledCfgGuard : public ripple::test::detail::FileDirGuard
{
private:
    path dataDir_;

    bool rmDataDir_{false};

    Config config_;

public:
    RippledCfgGuard(
        beast::unit_test::suite& test,
        path subDir,
        path const& dbPath,
        path const& validatorsFile,
        bool useCounter = true)
        : FileDirGuard(
              test,
              std::move(subDir),
              path(Config::configFileName),
              configContents(dbPath.string(), validatorsFile.string()),
              useCounter)
        , dataDir_(dbPath)
    {
        if (dbPath.empty())
            dataDir_ = subdir() / path(Config::databaseDirName);

        rmDataDir_ = !exists(dataDir_);
        config_.setup(
            file_.string(),
            /* bQuiet */ true,
            /* bSilent */ false,
            /* bStandalone */ false);
    }

    Config const&
    config() const
    {
        return config_;
    }

    std::string
    configFile() const
    {
        return file().string();
    }

    bool
    dataDirExists() const
    {
        return boost::filesystem::is_directory(dataDir_);
    }

    bool
    configFileExists() const
    {
        return fileExists();
    }

    ~RippledCfgGuard()
    {
        try
        {
            using namespace boost::filesystem;
            if (rmDataDir_)
                rmDir(dataDir_);
        }
        catch (std::exception& e)
        {
            // if we throw here, just let it die.
            test_.log << "Error in ~RippledCfgGuard: " << e.what() << std::endl;
        };
    }
};

std::string
valFileContents()
{
    std::string configContents(R"rippleConfig(
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
class ValidatorsTxtGuard : public test::detail::FileDirGuard
{
public:
    ValidatorsTxtGuard(
        beast::unit_test::suite& test,
        path subDir,
        path const& validatorsFileName,
        bool useCounter = true)
        : FileDirGuard(
              test,
              std::move(subDir),
              path(
                  validatorsFileName.empty() ? Config::validatorsFileName
                                             : validatorsFileName),
              valFileContents(),
              useCounter)
    {
    }

    bool
    validatorsFileExists() const
    {
        return fileExists();
    }

    std::string
    validatorsFile() const
    {
        return absolute(file()).string();
    }

    ~ValidatorsTxtGuard()
    {
    }
};
}  // namespace detail

class Config_test final : public TestSuite
{
private:
    using path = boost::filesystem::path;

public:
    void
    testLegacy()
    {
        testcase("legacy");

        Config c;

        std::string toLoad(R"rippleConfig(
[server]
port_rpc
port_peer
port_wss_admin

[ssl_verify]
0
)rippleConfig");

        c.loadFromString(toLoad);

        BEAST_EXPECT(c.legacy("ssl_verify") == "0");
        expectException([&c] { c.legacy("server"); });  // not a single line

        // set a legacy value
        BEAST_EXPECT(c.legacy("not_in_file") == "");
        c.legacy("not_in_file", "new_value");
        BEAST_EXPECT(c.legacy("not_in_file") == "new_value");
    }
    void
    testDbPath()
    {
        testcase("database_path");

        using namespace boost::filesystem;
        {
            boost::format cc("[database_path]\n%1%\n");

            auto const cwd = current_path();
            path const dataDirRel("test_data_dir");
            path const dataDirAbs(cwd / dataDirRel);
            {
                // Dummy test - do we get back what we put in
                Config c;
                c.loadFromString(boost::str(cc % dataDirAbs.string()));
                BEAST_EXPECT(c.legacy("database_path") == dataDirAbs.string());
            }
            {
                // Rel paths should convert to abs paths
                Config c;
                c.loadFromString(boost::str(cc % dataDirRel.string()));
                BEAST_EXPECT(c.legacy("database_path") == dataDirAbs.string());
            }
            {
                // No db section.
                // N.B. Config::setup will give database_path a default,
                // load will not.
                Config c;
                c.loadFromString("");
                BEAST_EXPECT(c.legacy("database_path") == "");
            }
        }
        {
            // read from file absolute path
            auto const cwd = current_path();
            ripple::test::detail::DirGuard const g0(*this, "test_db");
            path const dataDirRel("test_data_dir");
            path const dataDirAbs(cwd / g0.subdir() / dataDirRel);
            detail::RippledCfgGuard const g(
                *this, g0.subdir(), dataDirAbs, "", false);
            auto const& c(g.config());
            BEAST_EXPECT(g.dataDirExists());
            BEAST_EXPECT(g.configFileExists());
            BEAST_EXPECT(c.legacy("database_path") == dataDirAbs.string());
        }
        {
            // read from file relative path
            std::string const dbPath("my_db");
            detail::RippledCfgGuard const g(*this, "test_db", dbPath, "");
            auto const& c(g.config());
            std::string const nativeDbPath = absolute(path(dbPath)).string();
            BEAST_EXPECT(g.dataDirExists());
            BEAST_EXPECT(g.configFileExists());
            BEAST_EXPECT(c.legacy("database_path") == nativeDbPath);
        }
        {
            // read from file no path
            detail::RippledCfgGuard const g(*this, "test_db", "", "");
            auto const& c(g.config());
            std::string const nativeDbPath =
                absolute(g.subdir() / path(Config::databaseDirName)).string();
            BEAST_EXPECT(g.dataDirExists());
            BEAST_EXPECT(g.configFileExists());
            BEAST_EXPECT(c.legacy("database_path") == nativeDbPath);
        }
    }

    void
    testValidatorKeys()
    {
        testcase("validator keys");

        std::string const validationSeed = "spA4sh1qTvwq92X715tYyGQKmAKfa";

        auto const token =
            "eyJ2YWxpZGF0aW9uX3ByaXZhdGVfa2V5IjoiOWVkNDVmODY2MjQxY2MxOGEyNzQ3Yj"
            "U0Mzg3YzA2MjU5MDc5NzJmNGU3MTkwMjMxZmFhOTM3NDU3ZmE5ZGFmNiIsIm1hbmlm"
            "ZXN0IjoiSkFBQUFBRnhJZTFGdHdtaW12R3RIMmlDY01KcUM5Z1ZGS2lsR2Z3MS92Q3"
            "hIWFhMcGxjMkduTWhBa0UxYWdxWHhCd0R3RGJJRDZPTVNZdU0wRkRBbHBBZ05rOFNL"
            "Rm43TU8yZmRrY3dSUUloQU9uZ3U5c0FLcVhZb3VKK2wyVjBXK3NBT2tWQitaUlM2UF"
            "NobEpBZlVzWGZBaUJzVkpHZXNhYWRPSmMvYUFab2tTMXZ5bUdtVnJsSFBLV1gzWXl3"
            "dTZpbjhIQVNRS1B1Z0JENjdrTWFSRkd2bXBBVEhsR0tKZHZERmxXUFl5NUFxRGVkRn"
            "Y1VEphMncwaTIxZXEzTVl5d0xWSlpuRk9yN0Mwa3cyQWlUelNDakl6ZGl0UTg9In0"
            "=";

        {
            Config c;
            static boost::format configTemplate(R"rippleConfig(
[validation_seed]
%1%

[validator_token]
%2%
)rippleConfig");
            std::string error;
            auto const expectedError =
                "Cannot have both [validation_seed] "
                "and [validator_token] config sections";
            try
            {
                c.loadFromString(
                    boost::str(configTemplate % validationSeed % token));
            }
            catch (std::runtime_error& e)
            {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);
        }
    }

    void
    testNetworkID()
    {
        testcase("network id");
        std::string error;
        Config c;
        try
        {
            c.loadFromString(R"rippleConfig(
[network_id]
main
)rippleConfig");
        }
        catch (std::runtime_error& e)
        {
            error = e.what();
        }

        BEAST_EXPECT(error == "");
        BEAST_EXPECT(c.NETWORK_ID == 0);

        try
        {
            c.loadFromString(R"rippleConfig(
)rippleConfig");
        }
        catch (std::runtime_error& e)
        {
            error = e.what();
        }

        BEAST_EXPECT(error == "");
        BEAST_EXPECT(c.NETWORK_ID == 0);

        try
        {
            c.loadFromString(R"rippleConfig(
[network_id]
255
)rippleConfig");
        }
        catch (std::runtime_error& e)
        {
            error = e.what();
        }

        BEAST_EXPECT(error == "");
        BEAST_EXPECT(c.NETWORK_ID == 255);

        try
        {
            c.loadFromString(R"rippleConfig(
[network_id]
10000
)rippleConfig");
        }
        catch (std::runtime_error& e)
        {
            error = e.what();
        }

        BEAST_EXPECT(error == "");
        BEAST_EXPECT(c.NETWORK_ID == 10000);
    }

    void
    testValidatorsFile()
    {
        testcase("validators_file");

        using namespace boost::filesystem;
        {
            // load should throw for missing specified validators file
            boost::format cc("[validators_file]\n%1%\n");
            std::string error;
            std::string const missingPath = "/no/way/this/path/exists";
            auto const expectedError =
                "The file specified in [validators_file] does not exist: " +
                missingPath;
            try
            {
                Config c;
                c.loadFromString(boost::str(cc % missingPath));
            }
            catch (std::runtime_error& e)
            {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);
        }
        {
            // load should throw for invalid [validators_file]
            detail::ValidatorsTxtGuard const vtg(
                *this, "test_cfg", "validators.cfg");
            path const invalidFile = current_path() / vtg.subdir();
            boost::format cc("[validators_file]\n%1%\n");
            std::string error;
            auto const expectedError =
                "Invalid file specified in [validators_file]: " +
                invalidFile.string();
            try
            {
                Config c;
                c.loadFromString(boost::str(cc % invalidFile.string()));
            }
            catch (std::runtime_error& e)
            {
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
            c.loadFromString(toLoad);
            BEAST_EXPECT(c.legacy("validators_file").empty());
            BEAST_EXPECT(c.section(SECTION_VALIDATORS).values().size() == 5);
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
            c.loadFromString(toLoad);
            BEAST_EXPECT(
                c.section(SECTION_VALIDATOR_LIST_SITES).values().size() == 2);
            BEAST_EXPECT(
                c.section(SECTION_VALIDATOR_LIST_SITES).values()[0] ==
                "ripplevalidators.com");
            BEAST_EXPECT(
                c.section(SECTION_VALIDATOR_LIST_SITES).values()[1] ==
                "trustthesevalidators.gov");
            BEAST_EXPECT(
                c.section(SECTION_VALIDATOR_LIST_KEYS).values().size() == 1);
            BEAST_EXPECT(
                c.section(SECTION_VALIDATOR_LIST_KEYS).values()[0] ==
                "021A99A537FDEBC34E4FCA03B39BEADD04299BB19E85097EC92B15A3518801"
                "E566");
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
            try
            {
                c.loadFromString(toLoad);
            }
            catch (std::runtime_error& e)
            {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);
        }
        {
            // load from specified [validators_file] absolute path
            detail::ValidatorsTxtGuard const vtg(
                *this, "test_cfg", "validators.cfg");
            BEAST_EXPECT(vtg.validatorsFileExists());
            Config c;
            boost::format cc("[validators_file]\n%1%\n");
            c.loadFromString(boost::str(cc % vtg.validatorsFile()));
            BEAST_EXPECT(c.legacy("validators_file") == vtg.validatorsFile());
            BEAST_EXPECT(c.section(SECTION_VALIDATORS).values().size() == 8);
            BEAST_EXPECT(
                c.section(SECTION_VALIDATOR_LIST_SITES).values().size() == 2);
            BEAST_EXPECT(
                c.section(SECTION_VALIDATOR_LIST_KEYS).values().size() == 2);
        }
        {
            // load from specified [validators_file] file name
            // in config directory
            std::string const valFileName = "validators.txt";
            detail::ValidatorsTxtGuard const vtg(
                *this, "test_cfg", valFileName);
            detail::RippledCfgGuard const rcg(
                *this, vtg.subdir(), "", valFileName, false);
            BEAST_EXPECT(vtg.validatorsFileExists());
            BEAST_EXPECT(rcg.configFileExists());
            auto const& c(rcg.config());
            BEAST_EXPECT(c.legacy("validators_file") == valFileName);
            BEAST_EXPECT(c.section(SECTION_VALIDATORS).values().size() == 8);
            BEAST_EXPECT(
                c.section(SECTION_VALIDATOR_LIST_SITES).values().size() == 2);
            BEAST_EXPECT(
                c.section(SECTION_VALIDATOR_LIST_KEYS).values().size() == 2);
        }
        {
            // load from specified [validators_file] relative path
            // to config directory
            detail::ValidatorsTxtGuard const vtg(
                *this, "test_cfg", "validators.txt");
            auto const valFilePath = ".." / vtg.subdir() / "validators.txt";
            detail::RippledCfgGuard const rcg(
                *this, vtg.subdir(), "", valFilePath, false);
            BEAST_EXPECT(vtg.validatorsFileExists());
            BEAST_EXPECT(rcg.configFileExists());
            auto const& c(rcg.config());
            BEAST_EXPECT(c.legacy("validators_file") == valFilePath);
            BEAST_EXPECT(c.section(SECTION_VALIDATORS).values().size() == 8);
            BEAST_EXPECT(
                c.section(SECTION_VALIDATOR_LIST_SITES).values().size() == 2);
            BEAST_EXPECT(
                c.section(SECTION_VALIDATOR_LIST_KEYS).values().size() == 2);
        }
        {
            // load from validators file in default location
            detail::ValidatorsTxtGuard const vtg(
                *this, "test_cfg", "validators.txt");
            detail::RippledCfgGuard const rcg(
                *this, vtg.subdir(), "", "", false);
            BEAST_EXPECT(vtg.validatorsFileExists());
            BEAST_EXPECT(rcg.configFileExists());
            auto const& c(rcg.config());
            BEAST_EXPECT(c.legacy("validators_file").empty());
            BEAST_EXPECT(c.section(SECTION_VALIDATORS).values().size() == 8);
            BEAST_EXPECT(
                c.section(SECTION_VALIDATOR_LIST_SITES).values().size() == 2);
            BEAST_EXPECT(
                c.section(SECTION_VALIDATOR_LIST_KEYS).values().size() == 2);
        }
        {
            // load from specified [validators_file] instead
            // of default location
            detail::ValidatorsTxtGuard const vtg(
                *this, "test_cfg", "validators.cfg");
            BEAST_EXPECT(vtg.validatorsFileExists());
            detail::ValidatorsTxtGuard const vtgDefault(
                *this, vtg.subdir(), "validators.txt", false);
            BEAST_EXPECT(vtgDefault.validatorsFileExists());
            detail::RippledCfgGuard const rcg(
                *this, vtg.subdir(), "", vtg.validatorsFile(), false);
            BEAST_EXPECT(rcg.configFileExists());
            auto const& c(rcg.config());
            BEAST_EXPECT(c.legacy("validators_file") == vtg.validatorsFile());
            BEAST_EXPECT(c.section(SECTION_VALIDATORS).values().size() == 8);
            BEAST_EXPECT(
                c.section(SECTION_VALIDATOR_LIST_SITES).values().size() == 2);
            BEAST_EXPECT(
                c.section(SECTION_VALIDATOR_LIST_KEYS).values().size() == 2);
        }

        {
            // load validators from both config and validators file
            boost::format cc(R"rippleConfig(
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
            detail::ValidatorsTxtGuard const vtg(
                *this, "test_cfg", "validators.cfg");
            BEAST_EXPECT(vtg.validatorsFileExists());
            Config c;
            c.loadFromString(boost::str(cc % vtg.validatorsFile()));
            BEAST_EXPECT(c.legacy("validators_file") == vtg.validatorsFile());
            BEAST_EXPECT(c.section(SECTION_VALIDATORS).values().size() == 15);
            BEAST_EXPECT(
                c.section(SECTION_VALIDATOR_LIST_SITES).values().size() == 4);
            BEAST_EXPECT(
                c.section(SECTION_VALIDATOR_LIST_KEYS).values().size() == 3);
        }
        {
            // load should throw if [validators], [validator_keys] and
            // [validator_list_keys] are missing from rippled cfg and
            // validators file
            Config c;
            boost::format cc("[validators_file]\n%1%\n");
            std::string error;
            detail::ValidatorsTxtGuard const vtg(
                *this, "test_cfg", "validators.cfg");
            BEAST_EXPECT(vtg.validatorsFileExists());
            auto const expectedError =
                "The file specified in [validators_file] does not contain a "
                "[validators], [validator_keys] or [validator_list_keys] "
                "section: " +
                vtg.validatorsFile();
            std::ofstream o(vtg.validatorsFile());
            try
            {
                Config c2;
                c2.loadFromString(boost::str(cc % vtg.validatorsFile()));
            }
            catch (std::runtime_error& e)
            {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);
        }
    }

    void
    testSetup(bool explicitPath)
    {
        detail::RippledCfgGuard const cfg(
            *this, "testSetup", explicitPath ? "test_db" : "", "");
        /* RippledCfgGuard has a Config object that gets loaded on
            construction, but Config::setup is not reentrant, so we
            need a fresh config for every test case, so ignore it.
        */
        {
            Config config;
            config.setup(
                cfg.configFile(),
                /*bQuiet*/ false,
                /* bSilent */ false,
                /* bStandalone */ false);
            BEAST_EXPECT(!config.quiet());
            BEAST_EXPECT(!config.silent());
            BEAST_EXPECT(!config.standalone());
            BEAST_EXPECT(config.LEDGER_HISTORY == 256);
            BEAST_EXPECT(!config.legacy("database_path").empty());
        }
        {
            Config config;
            config.setup(
                cfg.configFile(),
                /*bQuiet*/ true,
                /* bSilent */ false,
                /* bStandalone */ false);
            BEAST_EXPECT(config.quiet());
            BEAST_EXPECT(!config.silent());
            BEAST_EXPECT(!config.standalone());
            BEAST_EXPECT(config.LEDGER_HISTORY == 256);
            BEAST_EXPECT(!config.legacy("database_path").empty());
        }
        {
            Config config;
            config.setup(
                cfg.configFile(),
                /*bQuiet*/ false,
                /* bSilent */ true,
                /* bStandalone */ false);
            BEAST_EXPECT(config.quiet());
            BEAST_EXPECT(config.silent());
            BEAST_EXPECT(!config.standalone());
            BEAST_EXPECT(config.LEDGER_HISTORY == 256);
            BEAST_EXPECT(!config.legacy("database_path").empty());
        }
        {
            Config config;
            config.setup(
                cfg.configFile(),
                /*bQuiet*/ true,
                /* bSilent */ true,
                /* bStandalone */ false);
            BEAST_EXPECT(config.quiet());
            BEAST_EXPECT(config.silent());
            BEAST_EXPECT(!config.standalone());
            BEAST_EXPECT(config.LEDGER_HISTORY == 256);
            BEAST_EXPECT(!config.legacy("database_path").empty());
        }
        {
            Config config;
            config.setup(
                cfg.configFile(),
                /*bQuiet*/ false,
                /* bSilent */ false,
                /* bStandalone */ true);
            BEAST_EXPECT(!config.quiet());
            BEAST_EXPECT(!config.silent());
            BEAST_EXPECT(config.standalone());
            BEAST_EXPECT(config.LEDGER_HISTORY == 0);
            BEAST_EXPECT(
                config.legacy("database_path").empty() == !explicitPath);
        }
        {
            Config config;
            config.setup(
                cfg.configFile(),
                /*bQuiet*/ true,
                /* bSilent */ false,
                /* bStandalone */ true);
            BEAST_EXPECT(config.quiet());
            BEAST_EXPECT(!config.silent());
            BEAST_EXPECT(config.standalone());
            BEAST_EXPECT(config.LEDGER_HISTORY == 0);
            BEAST_EXPECT(
                config.legacy("database_path").empty() == !explicitPath);
        }
        {
            Config config;
            config.setup(
                cfg.configFile(),
                /*bQuiet*/ false,
                /* bSilent */ true,
                /* bStandalone */ true);
            BEAST_EXPECT(config.quiet());
            BEAST_EXPECT(config.silent());
            BEAST_EXPECT(config.standalone());
            BEAST_EXPECT(config.LEDGER_HISTORY == 0);
            BEAST_EXPECT(
                config.legacy("database_path").empty() == !explicitPath);
        }
        {
            Config config;
            config.setup(
                cfg.configFile(),
                /*bQuiet*/ true,
                /* bSilent */ true,
                /* bStandalone */ true);
            BEAST_EXPECT(config.quiet());
            BEAST_EXPECT(config.silent());
            BEAST_EXPECT(config.standalone());
            BEAST_EXPECT(config.LEDGER_HISTORY == 0);
            BEAST_EXPECT(
                config.legacy("database_path").empty() == !explicitPath);
        }
    }

    void
    testPort()
    {
        detail::RippledCfgGuard const cfg(*this, "testPort", "", "");
        auto const& conf = cfg.config();
        if (!BEAST_EXPECT(conf.exists("port_rpc")))
            return;
        if (!BEAST_EXPECT(conf.exists("port_wss_admin")))
            return;
        ParsedPort rpc;
        if (!unexcept([&]() { parse_Port(rpc, conf["port_rpc"], log); }))
            return;
        BEAST_EXPECT(rpc.admin_nets_v4.size() + rpc.admin_nets_v6.size() == 2);
        ParsedPort wss;
        if (!unexcept([&]() { parse_Port(wss, conf["port_wss_admin"], log); }))
            return;
        BEAST_EXPECT(wss.admin_nets_v4.size() + wss.admin_nets_v6.size() == 1);
    }

    void
    testWhitespace()
    {
        Config cfg;
        /* NOTE: this string includes some explicit
         * space chars in order to verify proper trimming */
        std::string toLoad(R"(
[port_rpc])"
                           "\x20"
                           R"(
# comment
    # indented comment
)"
                           "\x20\x20"
                           R"(
[ips])"
                           "\x20"
                           R"(
r.ripple.com 51235

  [ips_fixed])"
                           "\x20\x20"
                           R"(
    # COMMENT
    s1.ripple.com 51235
    s2.ripple.com 51235

)");
        cfg.loadFromString(toLoad);
        BEAST_EXPECT(
            cfg.exists("port_rpc") && cfg.section("port_rpc").lines().empty() &&
            cfg.section("port_rpc").values().empty());
        BEAST_EXPECT(
            cfg.exists(SECTION_IPS) &&
            cfg.section(SECTION_IPS).lines().size() == 1 &&
            cfg.section(SECTION_IPS).values().size() == 1);
        BEAST_EXPECT(
            cfg.exists(SECTION_IPS_FIXED) &&
            cfg.section(SECTION_IPS_FIXED).lines().size() == 2 &&
            cfg.section(SECTION_IPS_FIXED).values().size() == 2);
    }

    void
    testColons()
    {
        Config cfg;
        /* NOTE: this string includes some explicit
         * space chars in order to verify proper trimming */
        std::string toLoad(R"(
[port_rpc])"
                           "\x20"
                           R"(
# comment
    # indented comment
)"
                           "\x20\x20"
                           R"(
[ips])"
                           "\x20"
                           R"(
r.ripple.com:51235

  [ips_fixed])"
                           "\x20\x20"
                           R"(
    # COMMENT
    s1.ripple.com:51235
    s2.ripple.com 51235
    anotherserversansport
    anotherserverwithport:12
    1.1.1.1:1
    1.1.1.1 1
    12.34.12.123:12345
    12.34.12.123 12345
    ::
    2001:db8::
    ::1
    ::1:12345
    [::1]:12345
    2001:db8:3333:4444:5555:6666:7777:8888:12345
    [2001:db8:3333:4444:5555:6666:7777:8888]:1


)");
        cfg.loadFromString(toLoad);
        BEAST_EXPECT(
            cfg.exists("port_rpc") && cfg.section("port_rpc").lines().empty() &&
            cfg.section("port_rpc").values().empty());
        BEAST_EXPECT(
            cfg.exists(SECTION_IPS) &&
            cfg.section(SECTION_IPS).lines().size() == 1 &&
            cfg.section(SECTION_IPS).values().size() == 1);
        BEAST_EXPECT(
            cfg.exists(SECTION_IPS_FIXED) &&
            cfg.section(SECTION_IPS_FIXED).lines().size() == 15 &&
            cfg.section(SECTION_IPS_FIXED).values().size() == 15);
        BEAST_EXPECT(cfg.IPS[0] == "r.ripple.com 51235");

        BEAST_EXPECT(cfg.IPS_FIXED[0] == "s1.ripple.com 51235");
        BEAST_EXPECT(cfg.IPS_FIXED[1] == "s2.ripple.com 51235");
        BEAST_EXPECT(cfg.IPS_FIXED[2] == "anotherserversansport");
        BEAST_EXPECT(cfg.IPS_FIXED[3] == "anotherserverwithport 12");
        BEAST_EXPECT(cfg.IPS_FIXED[4] == "1.1.1.1 1");
        BEAST_EXPECT(cfg.IPS_FIXED[5] == "1.1.1.1 1");
        BEAST_EXPECT(cfg.IPS_FIXED[6] == "12.34.12.123 12345");
        BEAST_EXPECT(cfg.IPS_FIXED[7] == "12.34.12.123 12345");

        // all ipv6 should be ignored by colon replacer, howsoever formated
        BEAST_EXPECT(cfg.IPS_FIXED[8] == "::");
        BEAST_EXPECT(cfg.IPS_FIXED[9] == "2001:db8::");
        BEAST_EXPECT(cfg.IPS_FIXED[10] == "::1");
        BEAST_EXPECT(cfg.IPS_FIXED[11] == "::1:12345");
        BEAST_EXPECT(cfg.IPS_FIXED[12] == "[::1]:12345");
        BEAST_EXPECT(
            cfg.IPS_FIXED[13] ==
            "2001:db8:3333:4444:5555:6666:7777:8888:12345");
        BEAST_EXPECT(
            cfg.IPS_FIXED[14] == "[2001:db8:3333:4444:5555:6666:7777:8888]:1");
    }

    void
    testComments()
    {
        struct TestCommentData
        {
            std::string_view line;
            std::string_view field;
            std::string_view expect;
            bool had_comment;
        };

        std::array<TestCommentData, 13> tests = {
            {{"password = aaaa\\#bbbb", "password", "aaaa#bbbb", false},
             {"password = aaaa#bbbb", "password", "aaaa", true},
             {"password = aaaa #bbbb", "password", "aaaa", true},
             // since the value is all comment, this doesn't parse as k=v :
             {"password = #aaaa #bbbb", "", "password =", true},
             {"password = aaaa\\# #bbbb", "password", "aaaa#", true},
             {"password = aaaa\\##bbbb", "password", "aaaa#", true},
             {"aaaa#bbbb", "", "aaaa", true},
             {"aaaa\\#bbbb", "", "aaaa#bbbb", false},
             {"aaaa\\##bbbb", "", "aaaa#", true},
             {"aaaa #bbbb", "", "aaaa", true},
             {"1 #comment", "", "1", true},
             {"#whole thing is comment", "", "", false},
             {"  #whole comment with space", "", "", false}}};

        for (auto const& t : tests)
        {
            Section s;
            s.append(t.line.data());
            BEAST_EXPECT(s.had_trailing_comments() == t.had_comment);
            if (t.field.empty())
            {
                BEAST_EXPECTS(s.legacy() == t.expect, s.legacy());
            }
            else
            {
                std::string field;
                BEAST_EXPECTS(set(field, t.field.data(), s), t.line);
                BEAST_EXPECTS(field == t.expect, t.line);
            }
        }

        {
            Section s;
            s.append("online_delete = 3000");
            std::uint32_t od = 0;
            BEAST_EXPECT(set(od, "online_delete", s));
            BEAST_EXPECTS(od == 3000, *(s.get<std::string>("online_delete")));
        }

        {
            Section s;
            s.append("online_delete = 2000 #my comment on this");
            std::uint32_t od = 0;
            BEAST_EXPECT(set(od, "online_delete", s));
            BEAST_EXPECTS(od == 2000, *(s.get<std::string>("online_delete")));
        }
    }

    void
    testGetters()
    {
        using namespace std::string_literals;
        Section s{"MySection"};
        s.append("a_string = mystring");
        s.append("positive_int = 2");
        s.append("negative_int = -3");
        s.append("bool_ish = 1");

        {
            auto val_1 = "value 1"s;
            BEAST_EXPECT(set(val_1, "a_string", s));
            BEAST_EXPECT(val_1 == "mystring");

            auto val_2 = "value 2"s;
            BEAST_EXPECT(!set(val_2, "not_a_key", s));
            BEAST_EXPECT(val_2 == "value 2");
            BEAST_EXPECT(!set(val_2, "default"s, "not_a_key", s));
            BEAST_EXPECT(val_2 == "default");

            auto val_3 = get<std::string>(s, "a_string");
            BEAST_EXPECT(val_3 == "mystring");
            auto val_4 = get<std::string>(s, "not_a_key");
            BEAST_EXPECT(val_4 == "");
            auto val_5 = get<std::string>(s, "not_a_key", "default");
            BEAST_EXPECT(val_5 == "default");

            auto val_6 = "value 6"s;
            BEAST_EXPECT(get_if_exists(s, "a_string", val_6));
            BEAST_EXPECT(val_6 == "mystring");

            auto val_7 = "value 7"s;
            BEAST_EXPECT(!get_if_exists(s, "not_a_key", val_7));
            BEAST_EXPECT(val_7 == "value 7");
        }

        {
            int val_1 = 1;
            BEAST_EXPECT(set(val_1, "positive_int", s));
            BEAST_EXPECT(val_1 == 2);

            int val_2 = 2;
            BEAST_EXPECT(set(val_2, "negative_int", s));
            BEAST_EXPECT(val_2 == -3);

            int val_3 = 3;
            BEAST_EXPECT(!set(val_3, "a_string", s));
            BEAST_EXPECT(val_3 == 3);

            auto val_4 = get<int>(s, "positive_int");
            BEAST_EXPECT(val_4 == 2);
            auto val_5 = get<int>(s, "not_a_key");
            BEAST_EXPECT(val_5 == 0);
            auto val_6 = get<int>(s, "not_a_key", 5);
            BEAST_EXPECT(val_6 == 5);
            auto val_7 = get<int>(s, "a_string", 6);
            BEAST_EXPECT(val_7 == 6);

            int val_8 = 8;
            BEAST_EXPECT(get_if_exists(s, "positive_int", val_8));
            BEAST_EXPECT(val_8 == 2);

            auto val_9 = 9;
            BEAST_EXPECT(!get_if_exists(s, "not_a_key", val_9));
            BEAST_EXPECT(val_9 == 9);

            auto val_10 = 10;
            BEAST_EXPECT(!get_if_exists(s, "a_string", val_10));
            BEAST_EXPECT(val_10 == 10);

            BEAST_EXPECT(s.get<int>("not_a_key") == std::nullopt);
            try
            {
                s.get<int>("a_string");
                fail();
            }
            catch (boost::bad_lexical_cast&)
            {
                pass();
            }
        }

        {
            bool flag_1 = false;
            BEAST_EXPECT(get_if_exists(s, "bool_ish", flag_1));
            BEAST_EXPECT(flag_1 == true);

            bool flag_2 = false;
            BEAST_EXPECT(!get_if_exists(s, "not_a_key", flag_2));
            BEAST_EXPECT(flag_2 == false);
        }
    }

    void
    testAmendment()
    {
        testcase("amendment");
        struct ConfigUnit
        {
            std::string unit;
            std::uint32_t numSeconds;
            std::uint32_t configVal;
            bool shouldPass;
        };

        std::vector<ConfigUnit> units = {
            {"seconds", 1, 15 * 60, false},
            {"minutes", 60, 14, false},
            {"minutes", 60, 15, true},
            {"hours", 3600, 10, true},
            {"days", 86400, 10, true},
            {"weeks", 604800, 2, true},
            {"months", 2592000, 1, false},
            {"years", 31536000, 1, false}};

        std::string space = "";
        for (auto& [unit, sec, val, shouldPass] : units)
        {
            Config c;
            std::string toLoad(R"rippleConfig(
[amendment_majority_time]
)rippleConfig");
            toLoad += std::to_string(val) + space + unit;
            space = space == "" ? " " : "";

            try
            {
                c.loadFromString(toLoad);
                if (shouldPass)
                    BEAST_EXPECT(
                        c.AMENDMENT_MAJORITY_TIME.count() == val * sec);
                else
                    fail();
            }
            catch (std::runtime_error&)
            {
                if (!shouldPass)
                    pass();
                else
                    fail();
            }
        }
    }

    void
    testOverlay()
    {
        testcase("overlay: unknown time");

        auto testUnknown =
            [](std::string value) -> std::optional<std::chrono::seconds> {
            try
            {
                Config c;
                c.loadFromString("[overlay]\nmax_unknown_time=" + value);
                return c.MAX_UNKNOWN_TIME;
            }
            catch (std::runtime_error&)
            {
                return {};
            }
        };

        // Failures
        BEAST_EXPECT(!testUnknown("none"));
        BEAST_EXPECT(!testUnknown("0.5"));
        BEAST_EXPECT(!testUnknown("180 seconds"));
        BEAST_EXPECT(!testUnknown("9 minutes"));

        // Below lower bound
        BEAST_EXPECT(!testUnknown("299"));

        // In bounds
        BEAST_EXPECT(testUnknown("300") == std::chrono::seconds{300});
        BEAST_EXPECT(testUnknown("301") == std::chrono::seconds{301});
        BEAST_EXPECT(testUnknown("1799") == std::chrono::seconds{1799});
        BEAST_EXPECT(testUnknown("1800") == std::chrono::seconds{1800});

        // Above upper bound
        BEAST_EXPECT(!testUnknown("1801"));

        testcase("overlay: diverged time");

        // In bounds:
        auto testDiverged =
            [](std::string value) -> std::optional<std::chrono::seconds> {
            try
            {
                Config c;
                c.loadFromString("[overlay]\nmax_diverged_time=" + value);
                return c.MAX_DIVERGED_TIME;
            }
            catch (std::runtime_error&)
            {
                return {};
            }
        };

        // Failures
        BEAST_EXPECT(!testDiverged("none"));
        BEAST_EXPECT(!testDiverged("0.5"));
        BEAST_EXPECT(!testDiverged("180 seconds"));
        BEAST_EXPECT(!testDiverged("9 minutes"));

        // Below lower bound
        BEAST_EXPECT(!testDiverged("0"));
        BEAST_EXPECT(!testDiverged("59"));

        // In bounds
        BEAST_EXPECT(testDiverged("60") == std::chrono::seconds{60});
        BEAST_EXPECT(testDiverged("61") == std::chrono::seconds{61});
        BEAST_EXPECT(testDiverged("899") == std::chrono::seconds{899});
        BEAST_EXPECT(testDiverged("900") == std::chrono::seconds{900});

        // Above upper bound
        BEAST_EXPECT(!testDiverged("901"));
    }

    void
    run() override
    {
        testLegacy();
        testDbPath();
        testValidatorKeys();
        testValidatorsFile();
        testSetup(false);
        testSetup(true);
        testPort();
        testWhitespace();
        testColons();
        testComments();
        testGetters();
        testAmendment();
        testOverlay();
        testNetworkID();
    }
};

BEAST_DEFINE_TESTSUITE(Config, core, ripple);

}  // namespace ripple
