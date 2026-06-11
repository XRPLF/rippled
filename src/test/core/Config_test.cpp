#include <test/jtx/TestSuite.h>
#include <test/unit_test/FileDirGuard.h>

#include <xrpld/core/Config.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/temp_dir.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/protocol/SystemParameters.h>  // IWYU pragma: keep
#include <xrpl/server/Port.h>

#include <boost/filesystem/operations.hpp>
#include <boost/format.hpp>  // IWYU pragma: keep
#include <boost/format/free_funcs.hpp>
#include <boost/lexical_cast/bad_lexical_cast.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <optional>
#include <ostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

namespace xrpl {
namespace detail {
std::string
configContents(std::string const& dbPath, std::string const& validatorsFile)
{
    static boost::format kConfigContentsTemplate(R"xrpldConfig(
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

# This is primary persistent datastore for xrpld.  This includes transaction
# metadata, account states, and ledger headers.  Helpful information can be
# found on https://xrpl.org/capacity-planning.html#node-db-type
# delete old ledgers while maintaining at least 2000. Do not require an
# external administrative command to initiate deletion.
[node_db]
type=memory
path=/Users/dummy/xrpld/config/db/rocksdb
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
/Users/dummy/xrpld/config/log/debug.log

[sntp_servers]
time.windows.com
time.apple.com
time.nist.gov
pool.ntp.org

# Where to find some other servers speaking the XRPL protocol.
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
)xrpldConfig");

    std::string dbPathSection = dbPath.empty() ? "" : "[database_path]\n" + dbPath;
    std::string valFileSection =
        validatorsFile.empty() ? "" : "[validators_file]\n" + validatorsFile;
    return boost::str(kConfigContentsTemplate % dbPathSection % valFileSection);
}

/**
   Write an xrpld config file and remove when done.
 */
class FileCfgGuard : public xrpl::detail::FileDirGuard
{
private:
    path dataDir_;

    bool rmDataDir_{false};

    Config config_;

public:
    FileCfgGuard(
        beast::unit_test::Suite& test,
        path subDir,
        path const& dbPath,
        path const& configFile,
        path const& validatorsFile,
        bool useCounter = true,
        std::string confContents = "")
        : FileDirGuard(
              test,
              std::move(subDir),
              configFile,
              confContents.empty() ? configContents(dbPath.string(), validatorsFile.string())
                                   : confContents,
              useCounter)
        , dataDir_(dbPath)
    {
        if (dbPath.empty())
            dataDir_ = subdir() / path(Config::kDatabaseDirName);

        rmDataDir_ = !exists(dataDir_);
        config_.setup(
            file_.string(),
            /* bQuiet */ true,
            /* bSilent */ false,
            /* bStandalone */ false);
    }

    [[nodiscard]] Config const&
    config() const
    {
        return config_;
    }

    [[nodiscard]] std::string
    configFile() const
    {
        return file().string();
    }

    [[nodiscard]] bool
    dataDirExists() const
    {
        return boost::filesystem::is_directory(dataDir_);
    }

    [[nodiscard]] bool
    configFileExists() const
    {
        return fileExists();
    }

    ~FileCfgGuard()
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
            test_.log << "Error in ~FileCfgGuard: " << e.what() << std::endl;
        };
    }
};

std::string
valFileContents()
{
    std::string configContents(R"xrpldConfig(
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
recommended-xrpl-validators.com
more-xrpl-validators.net

[validator_list_keys]
03E74EE14CB525AFBB9F1B7D86CD58ECC4B91452294B42AB4E78F260BD905C091D
030775A669685BD6ABCEBD80385921C7851783D991A8055FD21D2F3966C96F1B56

[validator_list_threshold]
2
)xrpldConfig");
    return configContents;
}

/**
   Write a validators.txt file and remove when done.
 */
class ValidatorsTxtGuard : public detail::FileDirGuard
{
public:
    ValidatorsTxtGuard(
        beast::unit_test::Suite& test,
        path subDir,
        path const& validatorsFileName,
        bool useCounter = true)
        : FileDirGuard(
              test,
              std::move(subDir),
              path(validatorsFileName.empty() ? Config::kValidatorsFileName : validatorsFileName),
              valFileContents(),
              useCounter)
    {
    }

    [[nodiscard]] bool
    validatorsFileExists() const
    {
        return fileExists();
    }

    [[nodiscard]] std::string
    validatorsFile() const
    {
        return absolute(file()).string();
    }

    ~ValidatorsTxtGuard() = default;
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

        std::string const toLoad(R"xrpldConfig(
[server]
port_rpc
port_peer
port_wss_admin

[ssl_verify]
0
)xrpldConfig");

        c.loadFromString(toLoad);

        BEAST_EXPECT(c.legacy(Sections::kSslVerify) == "0");
        expectException(
            [&c] { [[maybe_unused]] auto _ = c.legacy(Sections::kServer); });  // not a single line

        // set a legacy value
        BEAST_EXPECT(c.legacy("not_in_file").empty());
        c.legacy("not_in_file", "new_value");
        BEAST_EXPECT(c.legacy("not_in_file") == "new_value");
    }
    void
    testConfigFile()
    {
        testcase("config_file");

        using namespace boost::filesystem;
        auto const cwd = current_path();

        // Test both config file names.
        std::string_view const configFiles[] = {Config::kConfigFileName, Config::kConfigLegacyName};

        // Config file in current directory.
        for (auto const& configFile : configFiles)
        {
            // Use a temporary directory for testing.
            beast::TempDir const td;
            current_path(td.path());
            path const f = td.file(std::string{configFile});
            std::ofstream o(f.string());
            o << detail::configContents("", "");
            o.close();

            // Load the config file from the current directory and verify it.
            Config c;
            c.setup("", true, false, true);
            BEAST_EXPECT(c.section(Sections::kDebugLogfile).values().size() == 1);
            BEAST_EXPECT(
                c.section(Sections::kDebugLogfile).values()[0] ==
                "/Users/dummy/xrpld/config/log/debug.log");
        }

        // Config file in HOME or XDG_CONFIG_HOME directory.
#if BOOST_OS_LINUX || BOOST_OS_MACOS
        for (auto const& configFile : configFiles)
        {
            // Point the current working directory to a temporary directory, so
            // we don't pick up an actual config file from the repository root.
            beast::TempDir const td;
            current_path(td.path());

            // The XDG config directory is set: the config file must be in a
            // subdirectory named after the system.
            {
                beast::TempDir const tc;

                // Set the HOME and XDG_CONFIG_HOME environment variables. The
                // HOME variable is not used when XDG_CONFIG_HOME is set, but
                // must be set.
                char const* h = getenv("HOME");
                setenv("HOME", tc.path().c_str(), 1);
                char const* x = getenv("XDG_CONFIG_HOME");
                setenv("XDG_CONFIG_HOME", tc.path().c_str(), 1);

                // Create the config file in '${XDG_CONFIG_HOME}/[systemName]'.
                path p = tc.file(systemName());
                create_directory(p);
                p = tc.file(systemName() + "/" + std::string{configFile});
                std::ofstream o(p.string());
                o << detail::configContents("", "");
                o.close();

                // Load the config file from the config directory and verify it.
                Config c;
                c.setup("", true, false, true);
                BEAST_EXPECT(c.section(Sections::kDebugLogfile).values().size() == 1);
                BEAST_EXPECT(
                    c.section(Sections::kDebugLogfile).values()[0] ==
                    "/Users/dummy/xrpld/config/log/debug.log");

                // Restore the environment variables.
                (h != nullptr) ? setenv("HOME", h, 1) : unsetenv("HOME");
                (x != nullptr) ? setenv("XDG_CONFIG_HOME", x, 1) : unsetenv("XDG_CONFIG_HOME");
            }

            // The XDG config directory is not set: the config file must be in a
            // subdirectory named .config followed by the system name.
            {
                beast::TempDir const tc;

                // Set only the HOME environment variable.
                char const* h = getenv("HOME");
                setenv("HOME", tc.path().c_str(), 1);
                char const* x = getenv("XDG_CONFIG_HOME");
                unsetenv("XDG_CONFIG_HOME");

                // Create the config file in '${HOME}/.config/[systemName]'.
                std::string s = ".config";
                path p = tc.file(s);
                create_directory(p);
                s += "/" + systemName();
                p = tc.file(s);
                create_directory(p);
                p = tc.file(s + "/" + std::string{configFile});
                std::ofstream o(p.string());
                o << detail::configContents("", "");
                o.close();

                // Load the config file from the config directory and verify it.
                Config c;
                c.setup("", true, false, true);
                BEAST_EXPECT(c.section(Sections::kDebugLogfile).values().size() == 1);
                BEAST_EXPECT(
                    c.section(Sections::kDebugLogfile).values()[0] ==
                    "/Users/dummy/xrpld/config/log/debug.log");

                // Restore the environment variables.
                (h != nullptr) ? setenv("HOME", h, 1) : unsetenv("HOME");
                if (x != nullptr)
                    setenv("XDG_CONFIG_HOME", x, 1);
            }
        }
#endif

        // Restore the current working directory.
        current_path(cwd);
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
                BEAST_EXPECT(c.legacy(Sections::kDatabasePath) == dataDirAbs.string());
            }
            {
                // Rel paths should convert to abs paths
                Config c;
                c.loadFromString(boost::str(cc % dataDirRel.string()));
                BEAST_EXPECT(c.legacy(Sections::kDatabasePath) == dataDirAbs.string());
            }
            {
                // No db section.
                // N.B. Config::setup will give database_path a default,
                // load will not.
                Config c;
                c.loadFromString("");
                BEAST_EXPECT(c.legacy(Sections::kDatabasePath).empty());
            }
        }
        {
            // read from file absolute path
            auto const cwd = current_path();
            detail::DirGuard const g0(*this, "test_db");
            path const dataDirRel("test_data_dir");
            path const dataDirAbs(cwd / g0.subdir() / dataDirRel);
            detail::FileCfgGuard const g(
                *this, g0.subdir(), dataDirAbs, Config::kConfigFileName, "", false);
            auto const& c(g.config());
            BEAST_EXPECT(g.dataDirExists());
            BEAST_EXPECT(g.configFileExists());
            BEAST_EXPECT(c.legacy(Sections::kDatabasePath) == dataDirAbs.string());
        }
        {
            // read from file relative path
            std::string const dbPath("my_db");
            detail::FileCfgGuard const g(*this, "test_db", dbPath, Config::kConfigFileName, "");
            auto const& c(g.config());
            std::string const nativeDbPath = absolute(path(dbPath)).string();
            BEAST_EXPECT(g.dataDirExists());
            BEAST_EXPECT(g.configFileExists());
            BEAST_EXPECT(c.legacy(Sections::kDatabasePath) == nativeDbPath);
        }
        {
            // read from file no path
            detail::FileCfgGuard const g(*this, "test_db", "", Config::kConfigFileName, "");
            auto const& c(g.config());
            std::string const nativeDbPath =
                absolute(g.subdir() / path(Config::kDatabaseDirName)).string();
            BEAST_EXPECT(g.dataDirExists());
            BEAST_EXPECT(g.configFileExists());
            BEAST_EXPECT(c.legacy(Sections::kDatabasePath) == nativeDbPath);
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
            static boost::format kConfigTemplate(R"xrpldConfig(
[validation_seed]
%1%

[validator_token]
%2%
)xrpldConfig");
            std::string error;
            auto const expectedError =
                "Cannot have both [validation_seed] "
                "and [validator_token] config sections";
            try
            {
                c.loadFromString(boost::str(kConfigTemplate % validationSeed % token));
            }
            catch (std::runtime_error const& e)
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
            c.loadFromString(R"xrpldConfig(
[network_id]
main
)xrpldConfig");
        }
        catch (std::runtime_error const& e)
        {
            error = e.what();
        }

        BEAST_EXPECT(error.empty());
        BEAST_EXPECT(c.networkId == 0);

        try
        {
            c.loadFromString(R"xrpldConfig(
)xrpldConfig");
        }
        catch (std::runtime_error const& e)
        {
            error = e.what();
        }

        BEAST_EXPECT(error.empty());
        BEAST_EXPECT(c.networkId == 0);

        try
        {
            c.loadFromString(R"xrpldConfig(
[network_id]
255
)xrpldConfig");
        }
        catch (std::runtime_error const& e)
        {
            error = e.what();
        }

        BEAST_EXPECT(error.empty());
        BEAST_EXPECT(c.networkId == 255);

        try
        {
            c.loadFromString(R"xrpldConfig(
[network_id]
10000
)xrpldConfig");
        }
        catch (std::runtime_error const& e)
        {
            error = e.what();
        }

        BEAST_EXPECT(error.empty());
        BEAST_EXPECT(c.networkId == 10000);
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
                "The file specified in [validators_file] does not exist: " + missingPath;
            try
            {
                Config c;
                c.loadFromString(boost::str(cc % missingPath));
            }
            catch (std::runtime_error const& e)
            {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);
        }
        {
            // load should throw for invalid [validators_file]
            detail::ValidatorsTxtGuard const vtg(*this, "test_cfg", "validators.cfg");
            path const invalidFile = current_path() / vtg.subdir();
            boost::format cc("[validators_file]\n%1%\n");
            std::string error;
            auto const expectedError =
                "Invalid file specified in [validators_file]: " + invalidFile.string();
            try
            {
                Config c;
                c.loadFromString(boost::str(cc % invalidFile.string()));
            }
            catch (std::runtime_error const& e)
            {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);
        }
        {
            // load validators from config into single section
            Config c;
            std::string const toLoad(R"xrpldConfig(
[validators]
n949f75evCHwgyP4fPVgaHqNHxUVN15PsJEZ3B3HnXPcPjcZAoy7
n9MD5h24qrQqiyBC8aeqqCWvpiBiYQ3jxSr91uiDvmrkyHRdYLUj
n9L81uNCaPgtUJfaHh89gmdvXKAmSt5Gdsw2g1iPWaPkAHW5Nm4C

[validator_keys]
nHUhG1PgAG8H8myUENypM35JgfqXAKNQvRVVAFDRzJrny5eZN8d5
nHBu9PTL9dn2GuZtdW4U2WzBwffyX9qsQCd9CNU4Z5YG3PQfViM8
)xrpldConfig");
            c.loadFromString(toLoad);
            BEAST_EXPECT(c.legacy(Sections::kValidatorsFile).empty());
            BEAST_EXPECT(c.section(Sections::kValidators).values().size() == 5);
            BEAST_EXPECT(c.validatorListThreshold == std::nullopt);
        }
        {
            // load validator list sites and keys from config
            Config c;
            std::string const toLoad(R"xrpldConfig(
[validator_list_sites]
xrpl-validators.com
trust-these-validators.gov

[validator_list_keys]
021A99A537FDEBC34E4FCA03B39BEADD04299BB19E85097EC92B15A3518801E566

[validator_list_threshold]
1
)xrpldConfig");
            c.loadFromString(toLoad);
            BEAST_EXPECT(c.section(Sections::kValidatorListSites).values().size() == 2);
            BEAST_EXPECT(
                c.section(Sections::kValidatorListSites).values()[0] == "xrpl-validators.com");
            BEAST_EXPECT(
                c.section(Sections::kValidatorListSites).values()[1] ==
                "trust-these-validators.gov");
            BEAST_EXPECT(c.section(Sections::kValidatorListKeys).values().size() == 1);
            BEAST_EXPECT(
                c.section(Sections::kValidatorListKeys).values()[0] ==
                "021A99A537FDEBC34E4FCA03B39BEADD04299BB19E85097EC92B15A3518801"
                "E566");
            BEAST_EXPECT(c.section(Sections::kValidatorListThreshold).values().size() == 1);
            BEAST_EXPECT(c.section(Sections::kValidatorListThreshold).values()[0] == "1");
            BEAST_EXPECT(c.validatorListThreshold == std::size_t(1));
        }
        {
            // load validator list sites and keys from config
            Config c;
            std::string const toLoad(R"xrpldConfig(
[validator_list_sites]
xrpl-validators.com
trust-these-validators.gov

[validator_list_keys]
021A99A537FDEBC34E4FCA03B39BEADD04299BB19E85097EC92B15A3518801E566

[validator_list_threshold]
0
)xrpldConfig");
            c.loadFromString(toLoad);
            BEAST_EXPECT(c.section(Sections::kValidatorListSites).values().size() == 2);
            BEAST_EXPECT(
                c.section(Sections::kValidatorListSites).values()[0] == "xrpl-validators.com");
            BEAST_EXPECT(
                c.section(Sections::kValidatorListSites).values()[1] ==
                "trust-these-validators.gov");
            BEAST_EXPECT(c.section(Sections::kValidatorListKeys).values().size() == 1);
            BEAST_EXPECT(
                c.section(Sections::kValidatorListKeys).values()[0] ==
                "021A99A537FDEBC34E4FCA03B39BEADD04299BB19E85097EC92B15A3518801"
                "E566");
            BEAST_EXPECT(c.section(Sections::kValidatorListThreshold).values().size() == 1);
            BEAST_EXPECT(c.section(Sections::kValidatorListThreshold).values()[0] == "0");
            BEAST_EXPECT(c.validatorListThreshold == std::nullopt);
        }
        {
            // load should throw if [validator_list_threshold] is greater than
            // the number of [validator_list_keys]
            Config c;
            std::string const toLoad(R"xrpldConfig(
[validator_list_sites]
xrpl-validators.com
trust-these-validators.gov

[validator_list_keys]
021A99A537FDEBC34E4FCA03B39BEADD04299BB19E85097EC92B15A3518801E566

[validator_list_threshold]
2
)xrpldConfig");
            std::string error;
            auto const expectedError =
                "Value in config section [validator_list_threshold] exceeds "
                "the number of configured list keys";
            try
            {
                c.loadFromString(toLoad);
                fail();
            }
            catch (std::runtime_error const& e)
            {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);
        }
        {
            // load should throw if [validator_list_threshold] is malformed
            Config c;
            std::string const toLoad(R"xrpldConfig(
[validator_list_sites]
xrpl-validators.com
trust-these-validators.gov

[validator_list_keys]
021A99A537FDEBC34E4FCA03B39BEADD04299BB19E85097EC92B15A3518801E566

[validator_list_threshold]
value = 2
)xrpldConfig");
            std::string error;
            auto const expectedError =
                "Config section [validator_list_threshold] should contain "
                "single value only";
            try
            {
                c.loadFromString(toLoad);
                fail();
            }
            catch (std::runtime_error const& e)
            {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);
        }
        {
            // load should throw if [validator_list_threshold] is negative
            Config c;
            std::string const toLoad(R"xrpldConfig(
[validator_list_sites]
xrpl-validators.com
trust-these-validators.gov

[validator_list_keys]
021A99A537FDEBC34E4FCA03B39BEADD04299BB19E85097EC92B15A3518801E566

[validator_list_threshold]
-1
)xrpldConfig");
            bool error = false;
            try
            {
                c.loadFromString(toLoad);
                fail();
            }
            catch (std::bad_cast& e)
            {
                error = true;
            }
            BEAST_EXPECT(error);
        }
        {
            // load should throw if [validator_list_sites] is configured but
            // [validator_list_keys] is not
            Config c;
            std::string const toLoad(R"xrpldConfig(
[validator_list_sites]
xrpl-validators.com
trust-these-validators.gov
)xrpldConfig");
            std::string error;
            auto const expectedError = "[validator_list_keys] config section is missing";
            try
            {
                c.loadFromString(toLoad);
                fail();
            }
            catch (std::runtime_error const& e)
            {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);
        }
        {
            // load from specified [validators_file] absolute path
            detail::ValidatorsTxtGuard const vtg(*this, "test_cfg", "validators.cfg");
            BEAST_EXPECT(vtg.validatorsFileExists());
            Config c;
            boost::format cc("[validators_file]\n%1%\n");
            c.loadFromString(boost::str(cc % vtg.validatorsFile()));
            BEAST_EXPECT(c.legacy(Sections::kValidatorsFile) == vtg.validatorsFile());
            BEAST_EXPECT(c.section(Sections::kValidators).values().size() == 8);
            BEAST_EXPECT(c.section(Sections::kValidatorListSites).values().size() == 2);
            BEAST_EXPECT(c.section(Sections::kValidatorListKeys).values().size() == 2);
            BEAST_EXPECT(c.section(Sections::kValidatorListThreshold).values().size() == 1);
            BEAST_EXPECT(c.validatorListThreshold == 2);
        }
        {
            // load from specified [validators_file] file name
            // in config directory
            std::string const valFileName = "validators.txt";
            detail::ValidatorsTxtGuard const vtg(*this, "test_cfg", valFileName);
            detail::FileCfgGuard const rcg(
                *this, vtg.subdir(), "", Config::kConfigFileName, valFileName, false);
            BEAST_EXPECT(vtg.validatorsFileExists());
            BEAST_EXPECT(rcg.configFileExists());
            auto const& c(rcg.config());
            BEAST_EXPECT(c.legacy(Sections::kValidatorsFile) == valFileName);
            BEAST_EXPECT(c.section(Sections::kValidators).values().size() == 8);
            BEAST_EXPECT(c.section(Sections::kValidatorListSites).values().size() == 2);
            BEAST_EXPECT(c.section(Sections::kValidatorListKeys).values().size() == 2);
            BEAST_EXPECT(c.section(Sections::kValidatorListThreshold).values().size() == 1);
            BEAST_EXPECT(c.validatorListThreshold == 2);
        }
        {
            // load from specified [validators_file] relative path
            // to config directory
            detail::ValidatorsTxtGuard const vtg(*this, "test_cfg", "validators.txt");
            auto const valFilePath = ".." / vtg.subdir() / "validators.txt";
            detail::FileCfgGuard const rcg(
                *this, vtg.subdir(), "", Config::kConfigFileName, valFilePath, false);
            BEAST_EXPECT(vtg.validatorsFileExists());
            BEAST_EXPECT(rcg.configFileExists());
            auto const& c(rcg.config());
            BEAST_EXPECT(c.legacy(Sections::kValidatorsFile) == valFilePath);
            BEAST_EXPECT(c.section(Sections::kValidators).values().size() == 8);
            BEAST_EXPECT(c.section(Sections::kValidatorListSites).values().size() == 2);
            BEAST_EXPECT(c.section(Sections::kValidatorListKeys).values().size() == 2);
            BEAST_EXPECT(c.section(Sections::kValidatorListThreshold).values().size() == 1);
            BEAST_EXPECT(c.validatorListThreshold == 2);
        }
        {
            // load from validators file in default location
            detail::ValidatorsTxtGuard const vtg(*this, "test_cfg", "validators.txt");
            detail::FileCfgGuard const rcg(
                *this, vtg.subdir(), "", Config::kConfigFileName, "", false);
            BEAST_EXPECT(vtg.validatorsFileExists());
            BEAST_EXPECT(rcg.configFileExists());
            auto const& c(rcg.config());
            BEAST_EXPECT(c.legacy(Sections::kValidatorsFile).empty());
            BEAST_EXPECT(c.section(Sections::kValidators).values().size() == 8);
            BEAST_EXPECT(c.section(Sections::kValidatorListSites).values().size() == 2);
            BEAST_EXPECT(c.section(Sections::kValidatorListKeys).values().size() == 2);
            BEAST_EXPECT(c.section(Sections::kValidatorListThreshold).values().size() == 1);
            BEAST_EXPECT(c.validatorListThreshold == 2);
        }
        {
            // load from specified [validators_file] instead
            // of default location
            detail::ValidatorsTxtGuard const vtg(*this, "test_cfg", "validators.cfg");
            BEAST_EXPECT(vtg.validatorsFileExists());
            detail::ValidatorsTxtGuard const vtgDefault(
                *this, vtg.subdir(), "validators.txt", false);
            BEAST_EXPECT(vtgDefault.validatorsFileExists());
            detail::FileCfgGuard const rcg(
                *this, vtg.subdir(), "", Config::kConfigFileName, vtg.validatorsFile(), false);
            BEAST_EXPECT(rcg.configFileExists());
            auto const& c(rcg.config());
            BEAST_EXPECT(c.legacy(Sections::kValidatorsFile) == vtg.validatorsFile());
            BEAST_EXPECT(c.section(Sections::kValidators).values().size() == 8);
            BEAST_EXPECT(c.section(Sections::kValidatorListSites).values().size() == 2);
            BEAST_EXPECT(c.section(Sections::kValidatorListKeys).values().size() == 2);
            BEAST_EXPECT(c.section(Sections::kValidatorListThreshold).values().size() == 1);
            BEAST_EXPECT(c.validatorListThreshold == 2);
        }

        {
            // load validators from both config and validators file
            boost::format cc(R"xrpldConfig(
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
xrpl-validators.com
trust-these-validators.gov

[validator_list_keys]
021A99A537FDEBC34E4FCA03B39BEADD04299BB19E85097EC92B15A3518801E566
)xrpldConfig");
            detail::ValidatorsTxtGuard const vtg(*this, "test_cfg", "validators.cfg");
            BEAST_EXPECT(vtg.validatorsFileExists());
            Config c;
            c.loadFromString(boost::str(cc % vtg.validatorsFile()));
            BEAST_EXPECT(c.legacy(Sections::kValidatorsFile) == vtg.validatorsFile());
            BEAST_EXPECT(c.section(Sections::kValidators).values().size() == 15);
            BEAST_EXPECT(c.section(Sections::kValidatorListSites).values().size() == 4);
            BEAST_EXPECT(c.section(Sections::kValidatorListKeys).values().size() == 3);
            BEAST_EXPECT(c.section(Sections::kValidatorListThreshold).values().size() == 1);
            BEAST_EXPECT(c.validatorListThreshold == 2);
        }
        {
            // load should throw if [validator_list_threshold] is present both
            // in xrpld.cfg and validators file
            boost::format cc(R"xrpldConfig(
[validators_file]
%1%

[validator_list_threshold]
1
)xrpldConfig");
            std::string error;
            detail::ValidatorsTxtGuard const vtg(*this, "test_cfg", "validators.cfg");
            BEAST_EXPECT(vtg.validatorsFileExists());
            auto const expectedError =
                "Config section [validator_list_threshold] should contain "
                "single value only";
            try
            {
                Config c;
                c.loadFromString(boost::str(cc % vtg.validatorsFile()));
                fail();
            }
            catch (std::runtime_error const& e)
            {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);
        }
        {
            // load should throw if [validators], [validator_keys] and
            // [validator_list_keys] are missing from xrpld.cfg and
            // validators file
            Config const c;
            boost::format cc("[validators_file]\n%1%\n");
            std::string error;
            detail::ValidatorsTxtGuard const vtg(*this, "test_cfg", "validators.cfg");
            BEAST_EXPECT(vtg.validatorsFileExists());
            auto const expectedError =
                "The file specified in [validators_file] does not contain a "
                "[validators], [validator_keys] or [validator_list_keys] "
                "section: " +
                vtg.validatorsFile();
            std::ofstream const o(vtg.validatorsFile());
            try
            {
                Config c2;
                c2.loadFromString(boost::str(cc % vtg.validatorsFile()));
            }
            catch (std::runtime_error const& e)
            {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);
        }
    }

    void
    testSetup(bool explicitPath)
    {
        detail::FileCfgGuard const cfg(
            *this, "testSetup", explicitPath ? "test_db" : "", Config::kConfigFileName, "");
        /* FileCfgGuard has a Config object that gets loaded on
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
            BEAST_EXPECT(config.ledgerHistory == 256);
            BEAST_EXPECT(!config.legacy(Sections::kDatabasePath).empty());
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
            BEAST_EXPECT(config.ledgerHistory == 256);
            BEAST_EXPECT(!config.legacy(Sections::kDatabasePath).empty());
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
            BEAST_EXPECT(config.ledgerHistory == 256);
            BEAST_EXPECT(!config.legacy(Sections::kDatabasePath).empty());
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
            BEAST_EXPECT(config.ledgerHistory == 256);
            BEAST_EXPECT(!config.legacy(Sections::kDatabasePath).empty());
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
            BEAST_EXPECT(config.ledgerHistory == 0);
            BEAST_EXPECT(config.legacy(Sections::kDatabasePath).empty() == !explicitPath);
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
            BEAST_EXPECT(config.ledgerHistory == 0);
            BEAST_EXPECT(config.legacy(Sections::kDatabasePath).empty() == !explicitPath);
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
            BEAST_EXPECT(config.ledgerHistory == 0);
            BEAST_EXPECT(config.legacy(Sections::kDatabasePath).empty() == !explicitPath);
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
            BEAST_EXPECT(config.ledgerHistory == 0);
            BEAST_EXPECT(config.legacy(Sections::kDatabasePath).empty() == !explicitPath);
        }
    }

    void
    testPort()
    {
        detail::FileCfgGuard const cfg(*this, "testPort", "", Config::kConfigFileName, "");
        auto const& conf = cfg.config();
        if (!BEAST_EXPECT(conf.exists(Sections::kPortRpc)))
            return;
        if (!BEAST_EXPECT(conf.exists(Sections::kPortWssAdmin)))
            return;
        ParsedPort rpc;
        if (!unexcept([&]() { parsePort(rpc, conf[Sections::kPortRpc], log); }))
            return;
        BEAST_EXPECT(rpc.adminNetsV4.size() + rpc.adminNetsV6.size() == 2);
        ParsedPort wss;
        if (!unexcept([&]() { parsePort(wss, conf[Sections::kPortWssAdmin], log); }))
            return;
        BEAST_EXPECT(wss.adminNetsV4.size() + wss.adminNetsV6.size() == 1);
    }

    void
    testZeroPort()
    {
        auto const contents = std::regex_replace(
            detail::configContents("", ""), std::regex("port\\s*=\\s*\\d+"), "port = 0");

        try
        {
            detail::FileCfgGuard const cfg(
                *this, "testPort", "", Config::kConfigFileName, "", true, contents);
            BEAST_EXPECT(false);
        }
        catch (std::exception const& ex)
        {
            BEAST_EXPECT(
                std::string_view(ex.what()).starts_with("Invalid value '0' for key 'port'"));
        }
    }

    void
    testWhitespace()
    {
        Config cfg;
        /* NOTE: this string includes some explicit
         * space chars in order to verify proper trimming */
        std::string const toLoad(
            R"(
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
            cfg.exists(Sections::kPortRpc) && cfg.section(Sections::kPortRpc).lines().empty() &&
            cfg.section(Sections::kPortRpc).values().empty());
        BEAST_EXPECT(
            cfg.exists(Sections::kIps) && cfg.section(Sections::kIps).lines().size() == 1 &&
            cfg.section(Sections::kIps).values().size() == 1);
        BEAST_EXPECT(
            cfg.exists(Sections::kIpsFixed) &&
            cfg.section(Sections::kIpsFixed).lines().size() == 2 &&
            cfg.section(Sections::kIpsFixed).values().size() == 2);
    }

    void
    testColons()
    {
        Config cfg;
        /* NOTE: this string includes some explicit
         * space chars in order to verify proper trimming */
        std::string const toLoad(
            R"(
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
            cfg.exists(Sections::kPortRpc) && cfg.section(Sections::kPortRpc).lines().empty() &&
            cfg.section(Sections::kPortRpc).values().empty());
        BEAST_EXPECT(
            cfg.exists(Sections::kIps) && cfg.section(Sections::kIps).lines().size() == 1 &&
            cfg.section(Sections::kIps).values().size() == 1);
        BEAST_EXPECT(
            cfg.exists(Sections::kIpsFixed) &&
            cfg.section(Sections::kIpsFixed).lines().size() == 15 &&
            cfg.section(Sections::kIpsFixed).values().size() == 15);
        BEAST_EXPECT(cfg.ips[0] == "r.ripple.com 51235");

        BEAST_EXPECT(cfg.ipsFixed[0] == "s1.ripple.com 51235");
        BEAST_EXPECT(cfg.ipsFixed[1] == "s2.ripple.com 51235");
        BEAST_EXPECT(cfg.ipsFixed[2] == "anotherserversansport");
        BEAST_EXPECT(cfg.ipsFixed[3] == "anotherserverwithport 12");
        BEAST_EXPECT(cfg.ipsFixed[4] == "1.1.1.1 1");
        BEAST_EXPECT(cfg.ipsFixed[5] == "1.1.1.1 1");
        BEAST_EXPECT(cfg.ipsFixed[6] == "12.34.12.123 12345");
        BEAST_EXPECT(cfg.ipsFixed[7] == "12.34.12.123 12345");

        // all ipv6 should be ignored by colon replacer, howsoever formatted
        BEAST_EXPECT(cfg.ipsFixed[8] == "::");
        BEAST_EXPECT(cfg.ipsFixed[9] == "2001:db8::");
        BEAST_EXPECT(cfg.ipsFixed[10] == "::1");
        BEAST_EXPECT(cfg.ipsFixed[11] == "::1:12345");
        BEAST_EXPECT(cfg.ipsFixed[12] == "[::1]:12345");
        BEAST_EXPECT(cfg.ipsFixed[13] == "2001:db8:3333:4444:5555:6666:7777:8888:12345");
        BEAST_EXPECT(cfg.ipsFixed[14] == "[2001:db8:3333:4444:5555:6666:7777:8888]:1");
    }

    void
    testComments()
    {
        struct TestCommentData
        {
            std::string_view line;
            std::string_view field;
            std::string_view expect;
            bool hadComment;
        };

        std::array<TestCommentData, 13> const tests = {
            {{.line = "password = aaaa\\#bbbb",
              .field = "password",
              .expect = "aaaa#bbbb",
              .hadComment = false},
             {.line = "password = aaaa#bbbb",
              .field = "password",
              .expect = "aaaa",
              .hadComment = true},
             {.line = "password = aaaa #bbbb",
              .field = "password",
              .expect = "aaaa",
              .hadComment = true},
             // since the value is all comment, this doesn't parse as k=v :
             {.line = "password = #aaaa #bbbb",
              .field = "",
              .expect = "password =",
              .hadComment = true},
             {.line = "password = aaaa\\# #bbbb",
              .field = "password",
              .expect = "aaaa#",
              .hadComment = true},
             {.line = "password = aaaa\\##bbbb",
              .field = "password",
              .expect = "aaaa#",
              .hadComment = true},
             {.line = "aaaa#bbbb", .field = "", .expect = "aaaa", .hadComment = true},
             {.line = "aaaa\\#bbbb", .field = "", .expect = "aaaa#bbbb", .hadComment = false},
             {.line = "aaaa\\##bbbb", .field = "", .expect = "aaaa#", .hadComment = true},
             {.line = "aaaa #bbbb", .field = "", .expect = "aaaa", .hadComment = true},
             {.line = "1 #comment", .field = "", .expect = "1", .hadComment = true},
             {.line = "#whole thing is comment", .field = "", .expect = "", .hadComment = false},
             {.line = "  #whole comment with space",
              .field = "",
              .expect = "",
              .hadComment = false}}};

        for (auto const& t : tests)
        {
            Section s;
            s.append(std::string(t.line));
            BEAST_EXPECT(s.hadTrailingComments() == t.hadComment);
            if (t.field.empty())
            {
                BEAST_EXPECTS(s.legacy() == t.expect, s.legacy());
            }
            else
            {
                std::string field;
                BEAST_EXPECTS(set(field, std::string(t.field), s), t.line);
                BEAST_EXPECTS(field == t.expect, t.line);
            }
        }

        {
            Section s;
            s.append("online_delete = 3000");
            std::uint32_t od = 0;
            BEAST_EXPECT(set(od, Keys::kOnlineDelete, s));
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            BEAST_EXPECTS(od == 3000, *(s.get<std::string>(Keys::kOnlineDelete)));
        }

        {
            Section s;
            s.append("online_delete = 2000 #my comment on this");
            std::uint32_t od = 0;
            BEAST_EXPECT(set(od, Keys::kOnlineDelete, s));
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            BEAST_EXPECTS(od == 2000, *(s.get<std::string>(Keys::kOnlineDelete)));
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
            auto val1 = "value 1"s;
            BEAST_EXPECT(set(val1, "a_string", s));
            BEAST_EXPECT(val1 == "mystring");

            auto val2 = "value 2"s;
            BEAST_EXPECT(!set(val2, "not_a_key", s));
            BEAST_EXPECT(val2 == "value 2");
            BEAST_EXPECT(!set(val2, "default"s, "not_a_key", s));
            BEAST_EXPECT(val2 == "default");

            auto val3 = get<std::string>(s, "a_string");
            BEAST_EXPECT(val3 == "mystring");
            auto val4 = get<std::string>(s, "not_a_key");
            BEAST_EXPECT(val4.empty());
            auto val5 = get<std::string>(s, "not_a_key", "default");
            BEAST_EXPECT(val5 == "default");

            auto val6 = "value 6"s;
            BEAST_EXPECT(getIfExists(s, "a_string", val6));
            BEAST_EXPECT(val6 == "mystring");

            auto val7 = "value 7"s;
            BEAST_EXPECT(!getIfExists(s, "not_a_key", val7));
            BEAST_EXPECT(val7 == "value 7");
        }

        {
            int val1 = 1;
            BEAST_EXPECT(set(val1, "positive_int", s));
            BEAST_EXPECT(val1 == 2);

            int val2 = 2;
            BEAST_EXPECT(set(val2, "negative_int", s));
            BEAST_EXPECT(val2 == -3);

            int val3 = 3;
            BEAST_EXPECT(!set(val3, "a_string", s));
            BEAST_EXPECT(val3 == 3);

            auto val4 = get<int>(s, "positive_int");
            BEAST_EXPECT(val4 == 2);
            auto val5 = get<int>(s, "not_a_key");
            BEAST_EXPECT(val5 == 0);
            auto val6 = get<int>(s, "not_a_key", 5);
            BEAST_EXPECT(val6 == 5);
            auto val7 = get<int>(s, "a_string", 6);
            BEAST_EXPECT(val7 == 6);

            int val8 = 8;
            BEAST_EXPECT(getIfExists(s, "positive_int", val8));
            BEAST_EXPECT(val8 == 2);

            auto val9 = 9;
            BEAST_EXPECT(!getIfExists(s, "not_a_key", val9));
            BEAST_EXPECT(val9 == 9);

            auto val10 = 10;
            BEAST_EXPECT(!getIfExists(s, "a_string", val10));
            BEAST_EXPECT(val10 == 10);

            BEAST_EXPECT(s.get<int>("not_a_key") == std::nullopt);
            try
            {
                [[maybe_unused]] auto _ = s.get<int>("a_string");
                fail();
            }
            catch (boost::bad_lexical_cast&)
            {
                pass();
            }
        }

        {
            bool flag1 = false;
            BEAST_EXPECT(getIfExists(s, "bool_ish", flag1));
            BEAST_EXPECT(flag1 == true);

            bool flag2 = false;
            BEAST_EXPECT(!getIfExists(s, "not_a_key", flag2));
            BEAST_EXPECT(flag2 == false);
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

        std::vector<ConfigUnit> const units = {
            {.unit = "seconds", .numSeconds = 1, .configVal = 15 * 60, .shouldPass = false},
            {.unit = "minutes", .numSeconds = 60, .configVal = 14, .shouldPass = false},
            {.unit = "minutes", .numSeconds = 60, .configVal = 15, .shouldPass = true},
            {.unit = "hours", .numSeconds = 3600, .configVal = 10, .shouldPass = true},
            {.unit = "days", .numSeconds = 86400, .configVal = 10, .shouldPass = true},
            {.unit = "weeks", .numSeconds = 604800, .configVal = 2, .shouldPass = true},
            {.unit = "months", .numSeconds = 2592000, .configVal = 1, .shouldPass = false},
            {.unit = "years", .numSeconds = 31536000, .configVal = 1, .shouldPass = false}};

        std::string space;
        for (auto& [unit, sec, val, shouldPass] : units)
        {
            Config c;
            std::string toLoad(R"xrpldConfig(
[amendment_majority_time]
)xrpldConfig");
            toLoad += std::to_string(val) + space + unit;
            space = space.empty() ? " " : "";

            try
            {
                c.loadFromString(toLoad);
                if (shouldPass)
                {
                    BEAST_EXPECT(c.amendmentMajorityTime.count() == val * sec);
                }
                else
                {
                    fail();
                }
            }
            catch (std::runtime_error const&)
            {
                if (!shouldPass)
                {
                    pass();
                }
                else
                {
                    fail();
                }
            }
        }
    }

    void
    testOverlay()
    {
        testcase("overlay: unknown time");

        auto testUnknown = [](std::string value) -> std::optional<std::chrono::seconds> {
            try
            {
                Config c;
                c.loadFromString("[overlay]\nmax_unknown_time=" + value);
                return c.maxUnknownTime;
            }
            catch (std::runtime_error const&)
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
        auto testDiverged = [](std::string value) -> std::optional<std::chrono::seconds> {
            try
            {
                Config c;
                c.loadFromString("[overlay]\nmax_diverged_time=" + value);
                return c.maxDivergedTime;
            }
            catch (std::runtime_error const&)
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
        testConfigFile();
        testDbPath();
        testValidatorKeys();
        testValidatorsFile();
        testSetup(false);
        testSetup(true);
        testPort();
        testZeroPort();
        testWhitespace();
        testColons();
        testComments();
        testGetters();
        testAmendment();
        testOverlay();
        testNetworkID();
    }
};

BEAST_DEFINE_TESTSUITE(Config, core, xrpl);

}  // namespace xrpl
