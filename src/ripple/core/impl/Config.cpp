//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/basics/FileUtilities.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/contract.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/core/Config.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/json/json_reader.h>
#include <ripple/net/HTTPClient.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/SystemParameters.h>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/predef.h>
#include <boost/regex.hpp>
#include <boost/system/error_code.hpp>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <thread>

#if BOOST_OS_WINDOWS
#include <sysinfoapi.h>

namespace ripple {
namespace detail {

[[nodiscard]] std::uint64_t
getMemorySize()
{
    if (MEMORYSTATUSEX msx{sizeof(MEMORYSTATUSEX)}; GlobalMemoryStatusEx(&msx))
        return static_cast<std::uint64_t>(msx.ullTotalPhys);

    return 0;
}

}  // namespace detail
}  // namespace ripple
#endif

#if BOOST_OS_LINUX
#include <sys/sysinfo.h>

namespace ripple {
namespace detail {

[[nodiscard]] std::uint64_t
getMemorySize()
{
    struct sysinfo si;

    if (sysinfo(&si) == 0)
        return static_cast<std::uint64_t>(si.totalram);

    return 0;
}

}  // namespace detail
}  // namespace ripple

#endif

#if BOOST_OS_MACOS
#include <sys/sysctl.h>
#include <sys/types.h>

namespace ripple {
namespace detail {

[[nodiscard]] std::uint64_t
getMemorySize()
{
    int mib[] = {CTL_HW, HW_MEMSIZE};
    std::int64_t ram = 0;
    size_t size = sizeof(ram);

    if (sysctl(mib, 2, &ram, &size, NULL, 0) == 0)
        return static_cast<std::uint64_t>(ram);

    return 0;
}

}  // namespace detail
}  // namespace ripple
#endif

namespace ripple {

// clang-format off
// The configurable node sizes are "tiny", "small", "medium", "large", "huge"
inline constexpr std::array<std::pair<SizedItem, std::array<int, 5>>, 12>
sizedItems
{{
    // FIXME: We should document each of these items, explaining exactly
    //        what they control and whether there exists an explicit
    //        config option that can be used to override the default.

    //                                tiny    small   medium    large      huge
    {SizedItem::sweepInterval,   {{     10,      30,      60,      90,      120 }}},
    {SizedItem::treeCacheSize,   {{ 128000,  256000,  512000,  768000,  2048000 }}},
    {SizedItem::treeCacheAge,    {{     30,      60,      90,     120,      900 }}},
    {SizedItem::ledgerSize,      {{     32,     128,     256,     384,      768 }}},
    {SizedItem::ledgerAge,       {{     30,      90,     180,     240,      900 }}},
    {SizedItem::ledgerFetch,     {{      2,       3,       4,       5,        8 }}},
    {SizedItem::hashNodeDBCache, {{      4,      12,      24,      64,      128 }}},
    {SizedItem::txnDBCache,      {{      4,      12,      24,      64,      128 }}},
    {SizedItem::lgrDBCache,      {{      4,       8,      16,      32,      128 }}},
    {SizedItem::openFinalLimit,  {{      8,      16,      32,      64,      128 }}},
    {SizedItem::burstSize,       {{      4,       8,      16,      32,       48 }}},
    {SizedItem::ramSizeGB,       {{      8,      12,      16,      24,       32 }}},
}};

// Ensure that the order of entries in the table corresponds to the
// order of entries in the enum:
static_assert(
    []() constexpr->bool {
        std::underlying_type_t<SizedItem> idx = 0;

        for (auto const& i : sizedItems)
        {
            if (static_cast<std::underlying_type_t<SizedItem>>(i.first) != idx)
                return false;

            ++idx;
        }

        return true;
    }(),
    "Mismatch between sized item enum & array indices");
// clang-format on

//
// TODO: Check permissions on config file before using it.
//

#define SECTION_DEFAULT_NAME ""

IniFileSections
parseIniFile(std::string const& strInput, const bool bTrim)
{
    std::string strData(strInput);
    std::vector<std::string> vLines;
    IniFileSections secResult;

    // Convert DOS format to unix.
    boost::algorithm::replace_all(strData, "\r\n", "\n");

    // Convert MacOS format to unix.
    boost::algorithm::replace_all(strData, "\r", "\n");

    boost::algorithm::split(vLines, strData, boost::algorithm::is_any_of("\n"));

    // Set the default Section name.
    std::string strSection = SECTION_DEFAULT_NAME;

    // Initialize the default Section.
    secResult[strSection] = IniFileSections::mapped_type();

    // Parse each line.
    for (auto& strValue : vLines)
    {
        if (bTrim)
            boost::algorithm::trim(strValue);

        if (strValue.empty() || strValue[0] == '#')
        {
            // Blank line or comment, do nothing.
        }
        else if (strValue[0] == '[' && strValue[strValue.length() - 1] == ']')
        {
            // New Section.
            strSection = strValue.substr(1, strValue.length() - 2);
            secResult.emplace(strSection, IniFileSections::mapped_type{});
        }
        else
        {
            // Another line for Section.
            if (!strValue.empty())
                secResult[strSection].push_back(strValue);
        }
    }

    return secResult;
}

IniFileSections::mapped_type*
getIniFileSection(IniFileSections& secSource, std::string const& strSection)
{
    IniFileSections::iterator it;
    IniFileSections::mapped_type* smtResult;
    it = secSource.find(strSection);
    if (it == secSource.end())
        smtResult = nullptr;
    else
        smtResult = &(it->second);
    return smtResult;
}

bool
getSingleSection(
    IniFileSections& secSource,
    std::string const& strSection,
    std::string& strValue,
    beast::Journal j)
{
    IniFileSections::mapped_type* pmtEntries =
        getIniFileSection(secSource, strSection);
    bool bSingle = pmtEntries && 1 == pmtEntries->size();

    if (bSingle)
    {
        strValue = (*pmtEntries)[0];
    }
    else if (pmtEntries)
    {
        JLOG(j.warn()) << boost::str(
            boost::format("Section [%s]: requires 1 line not %d lines.") %
            strSection % pmtEntries->size());
    }

    return bSingle;
}

//------------------------------------------------------------------------------
//
// Config (DEPRECATED)
//
//------------------------------------------------------------------------------

char const* const Config::configFileName = "rippled.cfg";
char const* const Config::databaseDirName = "db";
char const* const Config::validatorsFileName = "validators.txt";

[[nodiscard]] static std::string
getEnvVar(char const* name)
{
    std::string value;

    if (auto const v = std::getenv(name); v != nullptr)
        value = v;

    return value;
}

constexpr FeeUnit32 Config::TRANSACTION_FEE_BASE;

Config::Config()
    : j_(beast::Journal::getNullSink()), ramSize_(detail::getMemorySize())
{
}

void
Config::setupControl(bool bQuiet, bool bSilent, bool bStandalone)
{
    assert(NODE_SIZE == 0);

    QUIET = bQuiet || bSilent;
    SILENT = bSilent;
    RUN_STANDALONE = bStandalone;

    // We try to autodetect the appropriate node size by checking available
    // RAM and CPU resources. We default to "tiny" for standalone mode.
    if (!bStandalone)
    {
        // First, check against 'minimum' RAM requirements per node size:
        auto const& threshold =
            sizedItems[std::underlying_type_t<SizedItem>(SizedItem::ramSizeGB)];

        auto ns = std::find_if(
            threshold.second.begin(),
            threshold.second.end(),
            [this](std::size_t limit) {
                return (ramSize_ / (1024 * 1024 * 1024)) < limit;
            });

        if (ns != threshold.second.end())
            NODE_SIZE = std::distance(threshold.second.begin(), ns);

        // Adjust the size based on the number of hardware threads of
        // execution available to us:
        if (auto const hc = std::thread::hardware_concurrency())
        {
            if (hc == 1)
                NODE_SIZE = 0;

            if (hc < 4)
                NODE_SIZE = std::min<std::size_t>(NODE_SIZE, 1);
        }
    }

    assert(NODE_SIZE <= 4);
}

void
Config::setup(
    std::string const& strConf,
    bool bQuiet,
    bool bSilent,
    bool bStandalone)
{
    boost::filesystem::path dataDir;
    std::string strDbPath, strConfFile;

    // Determine the config and data directories.
    // If the config file is found in the current working
    // directory, use the current working directory as the
    // config directory and that with "db" as the data
    // directory.

    setupControl(bQuiet, bSilent, bStandalone);

    strDbPath = databaseDirName;

    if (!strConf.empty())
        strConfFile = strConf;
    else
        strConfFile = configFileName;

    if (!strConf.empty())
    {
        // --conf=<path> : everything is relative that file.
        CONFIG_FILE = strConfFile;
        CONFIG_DIR = boost::filesystem::absolute(CONFIG_FILE);
        CONFIG_DIR.remove_filename();
        dataDir = CONFIG_DIR / strDbPath;
    }
    else
    {
        CONFIG_DIR = boost::filesystem::current_path();
        CONFIG_FILE = CONFIG_DIR / strConfFile;
        dataDir = CONFIG_DIR / strDbPath;

        // Construct XDG config and data home.
        // http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html
        auto const strHome = getEnvVar("HOME");
        auto strXdgConfigHome = getEnvVar("XDG_CONFIG_HOME");
        auto strXdgDataHome = getEnvVar("XDG_DATA_HOME");

        if (boost::filesystem::exists(CONFIG_FILE)
            // Can we figure out XDG dirs?
            || (strHome.empty() &&
                (strXdgConfigHome.empty() || strXdgDataHome.empty())))
        {
            // Current working directory is fine, put dbs in a subdir.
        }
        else
        {
            if (strXdgConfigHome.empty())
            {
                // $XDG_CONFIG_HOME was not set, use default based on $HOME.
                strXdgConfigHome = strHome + "/.config";
            }

            if (strXdgDataHome.empty())
            {
                // $XDG_DATA_HOME was not set, use default based on $HOME.
                strXdgDataHome = strHome + "/.local/share";
            }

            CONFIG_DIR = strXdgConfigHome + "/" + systemName();
            CONFIG_FILE = CONFIG_DIR / strConfFile;
            dataDir = strXdgDataHome + "/" + systemName();

            if (!boost::filesystem::exists(CONFIG_FILE))
            {
                CONFIG_DIR = "/etc/opt/" + systemName();
                CONFIG_FILE = CONFIG_DIR / strConfFile;
                dataDir = "/var/opt/" + systemName();
            }
        }
    }

    // Update default values
    load();
    if (exists("reporting"))
    {
        RUN_REPORTING = true;
        RUN_STANDALONE = true;
    }
    {
        // load() may have set a new value for the dataDir
        std::string const dbPath(legacy("database_path"));
        if (!dbPath.empty())
            dataDir = boost::filesystem::path(dbPath);
        else if (RUN_STANDALONE)
            dataDir.clear();
    }

    if (!dataDir.empty())
    {
        boost::system::error_code ec;
        boost::filesystem::create_directories(dataDir, ec);

        if (ec)
            Throw<std::runtime_error>(
                boost::str(boost::format("Can not create %s") % dataDir));

        legacy("database_path", boost::filesystem::absolute(dataDir).string());
    }

    HTTPClient::initializeSSLContext(*this, j_);

    if (RUN_STANDALONE)
        LEDGER_HISTORY = 0;

    std::string ledgerTxDbType;
    Section ledgerTxTablesSection = section("ledger_tx_tables");
    get_if_exists(ledgerTxTablesSection, "use_tx_tables", USE_TX_TABLES);
}

void
Config::load()
{
    // NOTE: this writes to cerr because we want cout to be reserved
    // for the writing of the json response (so that stdout can be part of a
    // pipeline, for instance)
    if (!QUIET)
        std::cerr << "Loading: " << CONFIG_FILE << "\n";

    boost::system::error_code ec;
    auto const fileContents = getFileContents(ec, CONFIG_FILE);

    if (ec)
    {
        std::cerr << "Failed to read '" << CONFIG_FILE << "'." << ec.value()
                  << ": " << ec.message() << std::endl;
        return;
    }

    loadFromString(fileContents);
}

void
Config::loadFromString(std::string const& fileContents)
{
    IniFileSections secConfig = parseIniFile(fileContents, true);

    build(secConfig);

    if (auto s = getIniFileSection(secConfig, SECTION_IPS))
        IPS = *s;

    if (auto s = getIniFileSection(secConfig, SECTION_IPS_FIXED))
        IPS_FIXED = *s;

    if (auto s = getIniFileSection(secConfig, SECTION_SNTP))
        SNTP_SERVERS = *s;

    {
        std::string dbPath;
        if (getSingleSection(secConfig, "database_path", dbPath, j_))
        {
            boost::filesystem::path p(dbPath);
            legacy("database_path", boost::filesystem::absolute(p).string());
        }
    }

    std::string strTemp;

    if (getSingleSection(secConfig, SECTION_PEER_PRIVATE, strTemp, j_))
        PEER_PRIVATE = beast::lexicalCastThrow<bool>(strTemp);

    if (getSingleSection(secConfig, SECTION_PEERS_MAX, strTemp, j_))
    {
        PEERS_MAX = beast::lexicalCastThrow<std::size_t>(strTemp);
    }
    else
    {
        std::optional<std::size_t> peers_in_max{};
        if (getSingleSection(secConfig, SECTION_PEERS_IN_MAX, strTemp, j_))
        {
            peers_in_max = beast::lexicalCastThrow<std::size_t>(strTemp);
            if (*peers_in_max > 1000)
                Throw<std::runtime_error>(
                    "Invalid value specified in [" SECTION_PEERS_IN_MAX
                    "] section; the value must be less or equal than 1000");
        }

        std::optional<std::size_t> peers_out_max{};
        if (getSingleSection(secConfig, SECTION_PEERS_OUT_MAX, strTemp, j_))
        {
            peers_out_max = beast::lexicalCastThrow<std::size_t>(strTemp);
            if (*peers_out_max < 10 || *peers_out_max > 1000)
                Throw<std::runtime_error>(
                    "Invalid value specified in [" SECTION_PEERS_OUT_MAX
                    "] section; the value must be in range 10-1000");
        }

        // if one section is configured then the other must be configured too
        if ((peers_in_max && !peers_out_max) ||
            (peers_out_max && !peers_in_max))
            Throw<std::runtime_error>("Both sections [" SECTION_PEERS_IN_MAX
                                      "]"
                                      "and [" SECTION_PEERS_OUT_MAX
                                      "] must be configured");

        if (peers_in_max && peers_out_max)
        {
            PEERS_IN_MAX = *peers_in_max;
            PEERS_OUT_MAX = *peers_out_max;
        }
    }

    if (getSingleSection(secConfig, SECTION_NODE_SIZE, strTemp, j_))
    {
        if (boost::iequals(strTemp, "tiny"))
            NODE_SIZE = 0;
        else if (boost::iequals(strTemp, "small"))
            NODE_SIZE = 1;
        else if (boost::iequals(strTemp, "medium"))
            NODE_SIZE = 2;
        else if (boost::iequals(strTemp, "large"))
            NODE_SIZE = 3;
        else if (boost::iequals(strTemp, "huge"))
            NODE_SIZE = 4;
        else
            NODE_SIZE = std::min<std::size_t>(
                4, beast::lexicalCastThrow<std::size_t>(strTemp));
    }

    if (getSingleSection(secConfig, SECTION_SIGNING_SUPPORT, strTemp, j_))
        signingEnabled_ = beast::lexicalCastThrow<bool>(strTemp);

    if (getSingleSection(secConfig, SECTION_ELB_SUPPORT, strTemp, j_))
        ELB_SUPPORT = beast::lexicalCastThrow<bool>(strTemp);

    getSingleSection(secConfig, SECTION_SSL_VERIFY_FILE, SSL_VERIFY_FILE, j_);
    getSingleSection(secConfig, SECTION_SSL_VERIFY_DIR, SSL_VERIFY_DIR, j_);

    if (getSingleSection(secConfig, SECTION_SSL_VERIFY, strTemp, j_))
        SSL_VERIFY = beast::lexicalCastThrow<bool>(strTemp);

    if (getSingleSection(secConfig, SECTION_RELAY_VALIDATIONS, strTemp, j_))
    {
        if (boost::iequals(strTemp, "all"))
            RELAY_UNTRUSTED_VALIDATIONS = true;
        else if (boost::iequals(strTemp, "trusted"))
            RELAY_UNTRUSTED_VALIDATIONS = false;
        else
            Throw<std::runtime_error>(
                "Invalid value specified in [" SECTION_RELAY_VALIDATIONS
                "] section");
    }

    if (getSingleSection(secConfig, SECTION_RELAY_PROPOSALS, strTemp, j_))
    {
        if (boost::iequals(strTemp, "all"))
            RELAY_UNTRUSTED_PROPOSALS = true;
        else if (boost::iequals(strTemp, "trusted"))
            RELAY_UNTRUSTED_PROPOSALS = false;
        else
            Throw<std::runtime_error>(
                "Invalid value specified in [" SECTION_RELAY_PROPOSALS
                "] section");
    }

    if (exists(SECTION_VALIDATION_SEED) && exists(SECTION_VALIDATOR_TOKEN))
        Throw<std::runtime_error>("Cannot have both [" SECTION_VALIDATION_SEED
                                  "] and [" SECTION_VALIDATOR_TOKEN
                                  "] config sections");

    if (getSingleSection(secConfig, SECTION_NETWORK_QUORUM, strTemp, j_))
        NETWORK_QUORUM = beast::lexicalCastThrow<std::size_t>(strTemp);

    if (getSingleSection(secConfig, SECTION_FEE_ACCOUNT_RESERVE, strTemp, j_))
        FEE_ACCOUNT_RESERVE = beast::lexicalCastThrow<std::uint64_t>(strTemp);

    if (getSingleSection(secConfig, SECTION_FEE_OWNER_RESERVE, strTemp, j_))
        FEE_OWNER_RESERVE = beast::lexicalCastThrow<std::uint64_t>(strTemp);

    if (getSingleSection(secConfig, SECTION_FEE_DEFAULT, strTemp, j_))
        FEE_DEFAULT = beast::lexicalCastThrow<std::uint64_t>(strTemp);

    if (getSingleSection(secConfig, SECTION_LEDGER_HISTORY, strTemp, j_))
    {
        if (boost::iequals(strTemp, "full"))
            LEDGER_HISTORY =
                std::numeric_limits<decltype(LEDGER_HISTORY)>::max();
        else if (boost::iequals(strTemp, "none"))
            LEDGER_HISTORY = 0;
        else
            LEDGER_HISTORY = beast::lexicalCastThrow<std::uint32_t>(strTemp);
    }

    if (getSingleSection(secConfig, SECTION_FETCH_DEPTH, strTemp, j_))
    {
        if (boost::iequals(strTemp, "none"))
            FETCH_DEPTH = 0;
        else if (boost::iequals(strTemp, "full"))
            FETCH_DEPTH = std::numeric_limits<decltype(FETCH_DEPTH)>::max();
        else
            FETCH_DEPTH = beast::lexicalCastThrow<std::uint32_t>(strTemp);

        if (FETCH_DEPTH < 10)
            FETCH_DEPTH = 10;
    }

    if (getSingleSection(secConfig, SECTION_PATH_SEARCH_OLD, strTemp, j_))
        PATH_SEARCH_OLD = beast::lexicalCastThrow<int>(strTemp);
    if (getSingleSection(secConfig, SECTION_PATH_SEARCH, strTemp, j_))
        PATH_SEARCH = beast::lexicalCastThrow<int>(strTemp);
    if (getSingleSection(secConfig, SECTION_PATH_SEARCH_FAST, strTemp, j_))
        PATH_SEARCH_FAST = beast::lexicalCastThrow<int>(strTemp);
    if (getSingleSection(secConfig, SECTION_PATH_SEARCH_MAX, strTemp, j_))
        PATH_SEARCH_MAX = beast::lexicalCastThrow<int>(strTemp);

    if (getSingleSection(secConfig, SECTION_DEBUG_LOGFILE, strTemp, j_))
        DEBUG_LOGFILE = strTemp;

    if (getSingleSection(secConfig, SECTION_WORKERS, strTemp, j_))
        WORKERS = beast::lexicalCastThrow<std::size_t>(strTemp);

    if (getSingleSection(secConfig, SECTION_COMPRESSION, strTemp, j_))
        COMPRESSION = beast::lexicalCastThrow<bool>(strTemp);

    if (getSingleSection(secConfig, SECTION_LEDGER_REPLAY, strTemp, j_))
        LEDGER_REPLAY = beast::lexicalCastThrow<bool>(strTemp);

    if (exists(SECTION_REDUCE_RELAY))
    {
        auto sec = section(SECTION_REDUCE_RELAY);
        VP_REDUCE_RELAY_ENABLE = sec.value_or("vp_enable", false);
        TX_REDUCE_RELAY_ENABLE = sec.value_or("tx_enable", false);
        REDUCE_RELAY_SQUELCH = sec.value_or("vp_squelch", false);
        TX_REDUCE_RELAY_MIN_PEERS = sec.value_or("tx_num_peers", 20);
        TX_RELAY_PERCENTAGE = sec.value_or("tx_relay_to_peers", 25);
        if (TX_RELAY_PERCENTAGE > 100 || TX_REDUCE_RELAY_MIN_PEERS < 20 ||
            static_cast<std::uint16_t>(
                100 * TX_REDUCE_RELAY_MIN_PEERS / TX_RELAY_PERCENTAGE) < 5)
            Throw<std::runtime_error>(
                "Invalid " SECTION_TX_REDUCE_RELAY
                ", num_peers must be greater or equal to 20"
                ", relay_to_peers must be less or equal to 100, and "
                "(100 * num_peers / relay_to_peers) must be greater or equal "
                "to 5");
    }

    if (getSingleSection(secConfig, SECTION_MAX_TRANSACTIONS, strTemp, j_))
    {
        MAX_TRANSACTIONS = std::clamp(
            beast::lexicalCastThrow<int>(strTemp),
            MIN_JOB_QUEUE_TX,
            MAX_JOB_QUEUE_TX);
    }

    if (getSingleSection(secConfig, SECTION_SERVER_DOMAIN, strTemp, j_))
    {
        if (!isProperlyFormedTomlDomain(strTemp))
        {
            Throw<std::runtime_error>(
                "Invalid " SECTION_SERVER_DOMAIN
                ": the domain name does not appear to meet the requirements.");
        }

        SERVER_DOMAIN = strTemp;
    }

    if (exists(SECTION_OVERLAY))
    {
        auto const sec = section(SECTION_OVERLAY);

        using namespace std::chrono;

        try
        {
            if (auto val = sec.get<std::string>("max_unknown_time"))
                MAX_UNKNOWN_TIME =
                    seconds{beast::lexicalCastThrow<std::uint32_t>(*val)};
        }
        catch (...)
        {
            Throw<std::runtime_error>(
                "Invalid value 'max_unknown_time' in " SECTION_OVERLAY
                ": must be of the form '<number>' representing seconds.");
        }

        if (MAX_UNKNOWN_TIME < seconds{300} || MAX_UNKNOWN_TIME > seconds{1800})
            Throw<std::runtime_error>(
                "Invalid value 'max_unknown_time' in " SECTION_OVERLAY
                ": the time must be between 300 and 1800 seconds, inclusive.");

        try
        {
            if (auto val = sec.get<std::string>("max_diverged_time"))
                MAX_DIVERGED_TIME =
                    seconds{beast::lexicalCastThrow<std::uint32_t>(*val)};
        }
        catch (...)
        {
            Throw<std::runtime_error>(
                "Invalid value 'max_diverged_time' in " SECTION_OVERLAY
                ": must be of the form '<number>' representing seconds.");
        }

        if (MAX_DIVERGED_TIME < seconds{60} || MAX_DIVERGED_TIME > seconds{900})
        {
            Throw<std::runtime_error>(
                "Invalid value 'max_diverged_time' in " SECTION_OVERLAY
                ": the time must be between 60 and 900 seconds, inclusive.");
        }
    }

    if (getSingleSection(
            secConfig, SECTION_AMENDMENT_MAJORITY_TIME, strTemp, j_))
    {
        using namespace std::chrono;
        boost::regex const re(
            "^\\s*(\\d+)\\s*(minutes|hours|days|weeks)\\s*(\\s+.*)?$");
        boost::smatch match;
        if (!boost::regex_match(strTemp, match, re))
            Throw<std::runtime_error>(
                "Invalid " SECTION_AMENDMENT_MAJORITY_TIME
                ", must be: [0-9]+ [minutes|hours|days|weeks]");

        std::uint32_t duration =
            beast::lexicalCastThrow<std::uint32_t>(match[1].str());

        if (boost::iequals(match[2], "minutes"))
            AMENDMENT_MAJORITY_TIME = minutes(duration);
        else if (boost::iequals(match[2], "hours"))
            AMENDMENT_MAJORITY_TIME = hours(duration);
        else if (boost::iequals(match[2], "days"))
            AMENDMENT_MAJORITY_TIME = days(duration);
        else if (boost::iequals(match[2], "weeks"))
            AMENDMENT_MAJORITY_TIME = weeks(duration);

        if (AMENDMENT_MAJORITY_TIME < minutes(15))
            Throw<std::runtime_error>(
                "Invalid " SECTION_AMENDMENT_MAJORITY_TIME
                ", the minimum amount of time an amendment must hold a "
                "majority is 15 minutes");
    }

    if (getSingleSection(secConfig, SECTION_BETA_RPC_API, strTemp, j_))
        BETA_RPC_API = beast::lexicalCastThrow<bool>(strTemp);

    // Do not load trusted validator configuration for standalone mode
    if (!RUN_STANDALONE)
    {
        // If a file was explicitly specified, then throw if the
        // path is malformed or if the file does not exist or is
        // not a file.
        // If the specified file is not an absolute path, then look
        // for it in the same directory as the config file.
        // If no path was specified, then look for validators.txt
        // in the same directory as the config file, but don't complain
        // if we can't find it.
        boost::filesystem::path validatorsFile;

        if (getSingleSection(secConfig, SECTION_VALIDATORS_FILE, strTemp, j_))
        {
            validatorsFile = strTemp;

            if (validatorsFile.empty())
                Throw<std::runtime_error>(
                    "Invalid path specified in [" SECTION_VALIDATORS_FILE "]");

            if (!validatorsFile.is_absolute() && !CONFIG_DIR.empty())
                validatorsFile = CONFIG_DIR / validatorsFile;

            if (!boost::filesystem::exists(validatorsFile))
                Throw<std::runtime_error>(
                    "The file specified in [" SECTION_VALIDATORS_FILE
                    "] "
                    "does not exist: " +
                    validatorsFile.string());

            else if (
                !boost::filesystem::is_regular_file(validatorsFile) &&
                !boost::filesystem::is_symlink(validatorsFile))
                Throw<std::runtime_error>(
                    "Invalid file specified in [" SECTION_VALIDATORS_FILE
                    "]: " +
                    validatorsFile.string());
        }
        else if (!CONFIG_DIR.empty())
        {
            validatorsFile = CONFIG_DIR / validatorsFileName;

            if (!validatorsFile.empty())
            {
                if (!boost::filesystem::exists(validatorsFile))
                    validatorsFile.clear();
                else if (
                    !boost::filesystem::is_regular_file(validatorsFile) &&
                    !boost::filesystem::is_symlink(validatorsFile))
                    validatorsFile.clear();
            }
        }

        if (!validatorsFile.empty() &&
            boost::filesystem::exists(validatorsFile) &&
            (boost::filesystem::is_regular_file(validatorsFile) ||
             boost::filesystem::is_symlink(validatorsFile)))
        {
            boost::system::error_code ec;
            auto const data = getFileContents(ec, validatorsFile);
            if (ec)
            {
                Throw<std::runtime_error>(
                    "Failed to read '" + validatorsFile.string() + "'." +
                    std::to_string(ec.value()) + ": " + ec.message());
            }

            auto iniFile = parseIniFile(data, true);

            auto entries = getIniFileSection(iniFile, SECTION_VALIDATORS);

            if (entries)
                section(SECTION_VALIDATORS).append(*entries);

            auto valKeyEntries =
                getIniFileSection(iniFile, SECTION_VALIDATOR_KEYS);

            if (valKeyEntries)
                section(SECTION_VALIDATOR_KEYS).append(*valKeyEntries);

            auto valSiteEntries =
                getIniFileSection(iniFile, SECTION_VALIDATOR_LIST_SITES);

            if (valSiteEntries)
                section(SECTION_VALIDATOR_LIST_SITES).append(*valSiteEntries);

            auto valListKeys =
                getIniFileSection(iniFile, SECTION_VALIDATOR_LIST_KEYS);

            if (valListKeys)
                section(SECTION_VALIDATOR_LIST_KEYS).append(*valListKeys);

            if (!entries && !valKeyEntries && !valListKeys)
                Throw<std::runtime_error>(
                    "The file specified in [" SECTION_VALIDATORS_FILE
                    "] "
                    "does not contain a [" SECTION_VALIDATORS
                    "], "
                    "[" SECTION_VALIDATOR_KEYS
                    "] or "
                    "[" SECTION_VALIDATOR_LIST_KEYS
                    "]"
                    " section: " +
                    validatorsFile.string());
        }

        // Consolidate [validator_keys] and [validators]
        section(SECTION_VALIDATORS)
            .append(section(SECTION_VALIDATOR_KEYS).lines());

        if (!section(SECTION_VALIDATOR_LIST_SITES).lines().empty() &&
            section(SECTION_VALIDATOR_LIST_KEYS).lines().empty())
        {
            Throw<std::runtime_error>(
                "[" + std::string(SECTION_VALIDATOR_LIST_KEYS) +
                "] config section is missing");
        }
    }

    {
        auto const part = section("features");
        for (auto const& s : part.values())
        {
            if (auto const f = getRegisteredFeature(s))
                features.insert(*f);
            else
                Throw<std::runtime_error>(
                    "Unknown feature: " + s + "  in config file.");
        }
    }

    // This doesn't properly belong here, but check to make sure that the
    // value specified for network_quorum is achievable:
    {
        auto pm = PEERS_MAX;

        // FIXME this apparently magic value is actually defined as a constant
        //       elsewhere (see defaultMaxPeers) but we handle this check here.
        if (pm == 0)
            pm = 21;

        if (NETWORK_QUORUM > pm)
        {
            Throw<std::runtime_error>(
                "The minimum number of required peers (network_quorum) exceeds "
                "the maximum number of allowed peers (peers_max)");
        }
    }
}

boost::filesystem::path
Config::getDebugLogFile() const
{
    auto log_file = DEBUG_LOGFILE;

    if (!log_file.empty() && !log_file.is_absolute())
    {
        // Unless an absolute path for the log file is specified, the
        // path is relative to the config file directory.
        log_file = boost::filesystem::absolute(log_file, CONFIG_DIR);
    }

    if (!log_file.empty())
    {
        auto log_dir = log_file.parent_path();

        if (!boost::filesystem::is_directory(log_dir))
        {
            boost::system::error_code ec;
            boost::filesystem::create_directories(log_dir, ec);

            // If we fail, we warn but continue so that the calling code can
            // decide how to handle this situation.
            if (ec)
            {
                std::cerr << "Unable to create log file path " << log_dir
                          << ": " << ec.message() << '\n';
            }
        }
    }

    return log_file;
}

int
Config::getValueFor(SizedItem item, std::optional<std::size_t> node) const
{
    auto const index = static_cast<std::underlying_type_t<SizedItem>>(item);
    assert(index < sizedItems.size());
    assert(!node || *node <= 4);
    return sizedItems.at(index).second.at(node.value_or(NODE_SIZE));
}

}  // namespace ripple
