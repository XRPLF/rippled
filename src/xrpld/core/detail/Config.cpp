#include <xrpld/core/Config.h>

#include <xrpl/basics/FileUtilities.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/net/HTTPClient.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/rdb/DBInit.h>
#include <xrpl/rdb/DatabaseCon.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/format/free_funcs.hpp>
#include <boost/multiprecision/detail/endian.hpp>
#include <boost/predef.h>
#include <boost/regex.hpp>  // IWYU pragma: keep
#include <boost/regex/v5/regex.hpp>
#include <boost/regex/v5/regex_match.hpp>
#include <boost/system/detail/error_code.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#if BOOST_OS_WINDOWS
#include <sysinfoapi.h>

namespace xrpl::detail {

[[nodiscard]] std::uint64_t
getMemorySize()
{
    if (MEMORYSTATUSEX msx{sizeof(MEMORYSTATUSEX)}; GlobalMemoryStatusEx(&msx))
        return static_cast<std::uint64_t>(msx.ullTotalPhys);

    return 0;
}

}  // namespace xrpl::detail

#endif

#if BOOST_OS_LINUX
#include <sys/sysinfo.h>  // IWYU pragma: keep

namespace xrpl::detail {

[[nodiscard]] std::uint64_t
getMemorySize()
{
    if (struct sysinfo si{}; sysinfo(&si) == 0)
        return static_cast<std::uint64_t>(si.totalram) * si.mem_unit;

    return 0;
}

}  // namespace xrpl::detail

#endif

#if BOOST_OS_MACOS
#include <sys/sysctl.h>

namespace xrpl::detail {

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

}  // namespace xrpl::detail

#endif

namespace xrpl {

// clang-format off
// The configurable node sizes are "tiny", "small", "medium", "large", "huge"
inline constexpr std::array<std::pair<SizedItem, std::array<int, 5>>, 13>
kSizedItems
{{
    // FIXME: We should document each of these items, explaining exactly
    //        what they control and whether there exists an explicit
    //        config option that can be used to override the default.

    //                                   tiny    small   medium    large     huge
    {SizedItem::SweepInterval,      {{     10,      30,      60,      90,     120 }}},
    {SizedItem::TreeCacheSize,      {{ 262144,  524288, 2097152, 4194304, 8388608 }}},
    {SizedItem::TreeCacheAge,       {{     30,      60,      90,     120,     900 }}},
    {SizedItem::LedgerSize,         {{     32,      32,      64,     256,     384 }}},
    {SizedItem::LedgerAge,          {{     30,      60,     180,     300,     600 }}},
    {SizedItem::LedgerFetch,        {{      2,       3,       4,       5,       8 }}},
    {SizedItem::HashNodeDbCache,    {{      4,      12,      24,      64,     128 }}},
    {SizedItem::TxnDbCache,         {{      4,      12,      24,      64,     128 }}},
    {SizedItem::LgrDbCache,         {{      4,       8,      16,      32,     128 }}},
    {SizedItem::OpenFinalLimit,     {{      8,      16,      32,      64,     128 }}},
    {SizedItem::BurstSize,          {{      4,       8,      16,      32,      48 }}},
    {SizedItem::RamSizeGb,          {{      6,       8,      12,      24,       0 }}},
    {SizedItem::AccountIdCacheSize, {{  20047,   50053,   77081,  150061,  300007 }}}
}};
// clang-format on

// Ensure that the order of entries in the table corresponds to the
// order of entries in the enum:
static_assert(
    []() constexpr -> bool {
        std::underlying_type_t<SizedItem> idx = 0;

        for (auto const& i : kSizedItems)
        {
            if (static_cast<std::underlying_type_t<SizedItem>>(i.first) != idx)
                return false;

            ++idx;
        }

        return true;
    }(),
    "Mismatch between sized item enum & array indices");

//
// TODO: Check permissions on config file before using it.
//

#define SECTION_DEFAULT_NAME ""

IniFileSections
parseIniFile(std::string const& strInput, bool const bTrim)
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
    std::string strSection = SECTION_DEFAULT_NAME;  // NOLINT(readability-redundant-string-init)

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
    if (auto it = secSource.find(strSection); it != secSource.end())
        return &(it->second);

    return nullptr;
}

bool
getSingleSection(
    IniFileSections& secSource,
    std::string const& strSection,
    std::string& strValue,
    beast::Journal j)
{
    auto const pmtEntries = getIniFileSection(secSource, strSection);

    if ((pmtEntries != nullptr) && pmtEntries->size() == 1)
    {
        strValue = (*pmtEntries)[0];
        return true;
    }

    if (pmtEntries != nullptr)
    {
        JLOG(j.warn()) << "Section '" << strSection << "': requires 1 line not "
                       << pmtEntries->size() << " lines.";
    }

    return false;
}

//------------------------------------------------------------------------------
//
// Config
//
//------------------------------------------------------------------------------

char const* const Config::kConfigFileName = "xrpld.cfg";
char const* const Config::kConfigLegacyName = "rippled.cfg";
char const* const Config::kDatabaseDirName = "db";
char const* const Config::kValidatorsFileName = "validators.txt";

[[nodiscard]] static std::string
getEnvVar(char const* name)
{
    std::string value;

    if (auto const v = std::getenv(name); v != nullptr)
        value = v;

    return value;
}

Config::Config()
    : j_(beast::Journal::getNullSink()), ramSize_(detail::getMemorySize() / (1024 * 1024 * 1024))
{
}

void
Config::setupControl(bool bQuiet, bool bSilent, bool bStandalone)
{
    XRPL_ASSERT(nodeSize == 0, "xrpl::Config::setupControl : node size not set");

    quiet_ = bQuiet || bSilent;
    silent_ = bSilent;
    runStandalone_ = bStandalone;

    // We try to autodetect the appropriate node size by checking available
    // RAM and CPU resources. We default to "tiny" for standalone mode.
    if (!bStandalone)
    {
        // First, check against 'minimum' RAM requirements per node size:
        auto const& threshold =
            kSizedItems[std::underlying_type_t<SizedItem>(SizedItem::RamSizeGb)];

        auto ns = std::ranges::find_if(threshold.second, [this](std::size_t limit) {
            return (limit == 0) || (ramSize_ < limit);
        });

        XRPL_ASSERT(ns != threshold.second.end(), "xrpl::Config::setupControl : valid node size");

        if (ns != threshold.second.end())
            nodeSize = std::distance(threshold.second.begin(), ns);

        // Adjust the size based on the number of hardware threads of
        // execution available to us:
        if (auto const hc = std::thread::hardware_concurrency(); hc != 0)
            nodeSize = std::min<std::size_t>(hc / 2, nodeSize);
    }

    XRPL_ASSERT(nodeSize <= 4, "xrpl::Config::setupControl : node size is set");
}

void
Config::setup(std::string const& strConf, bool bQuiet, bool bSilent, bool bStandalone)
{
    setupControl(bQuiet, bSilent, bStandalone);

    // Determine the config and data directories.
    // If the config file is found in the current working
    // directory, use the current working directory as the
    // config directory and that with "db" as the data
    // directory.
    boost::filesystem::path dataDir;

    if (!strConf.empty())
    {
        // --conf=<path> : everything is relative that file.
        configFile_ = strConf;
        configDir = boost::filesystem::absolute(configFile_);
        configDir.remove_filename();
        dataDir = configDir / kDatabaseDirName;
    }
    else
    {
        do
        {
            // Check if either of the config files exist in the current working
            // directory, in which case the databases will be stored in a
            // subdirectory.
            configDir = boost::filesystem::current_path();
            dataDir = configDir / kDatabaseDirName;
            configFile_ = configDir / kConfigFileName;
            if (boost::filesystem::exists(configFile_))
                break;
            configFile_ = configDir / kConfigLegacyName;
            if (boost::filesystem::exists(configFile_))
                break;

            // Check if the home directory is set, and optionally the XDG config
            // and/or data directories, as the config may be there. See
            // http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html.
            auto const strHome = getEnvVar("HOME");
            if (!strHome.empty())
            {
                auto strXdgConfigHome = getEnvVar("XDG_CONFIG_HOME");
                auto strXdgDataHome = getEnvVar("XDG_DATA_HOME");
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

                // Check if either of the config files exist in the XDG config
                // dir.
                dataDir = strXdgDataHome + "/" + systemName();
                configDir = strXdgConfigHome + "/" + systemName();
                configFile_ = configDir / kConfigFileName;
                if (boost::filesystem::exists(configFile_))
                    break;
                configFile_ = configDir / kConfigLegacyName;
                if (boost::filesystem::exists(configFile_))
                    break;
            }

            // As a last resort, check the system config directory.
            dataDir = "/var/lib/" + systemName();
            configDir = "/etc/" + systemName();
            configFile_ = configDir / kConfigFileName;
            if (boost::filesystem::exists(configFile_))
                break;
            configFile_ = configDir / kConfigLegacyName;
        } while (false);
    }

    // Update default values
    load();
    {
        // load() may have set a new value for the dataDir
        std::string const dbPath(legacy(Sections::kDatabasePath));
        if (!dbPath.empty())
        {
            dataDir = boost::filesystem::path(dbPath);
        }
        else if (runStandalone_)
        {
            dataDir.clear();
        }
    }

    if (!dataDir.empty())
    {
        boost::system::error_code ec;
        boost::filesystem::create_directories(dataDir, ec);

        if (ec)
            Throw<std::runtime_error>(boost::str(boost::format("Can not create %s") % dataDir));

        legacy(Sections::kDatabasePath, boost::filesystem::absolute(dataDir).string());
    }

    HTTPClient::initializeSSLContext(this->sslVerifyDir, this->sslVerifyFile, this->sslVerify, j_);

    if (runStandalone_)
        ledgerHistory = 0;

    Section const ledgerTxTablesSection = section(Sections::kLedgerTxTables);
    getIfExists(ledgerTxTablesSection, Keys::kUseTxTables, useTxTables_);

    Section const& nodeDbSection{section(Sections::kNodeDatabase)};
    getIfExists(nodeDbSection, Keys::kFastLoad, fastLoad);
}

// 0 ports are allowed for unit tests, but still not allowed to be present in
// config file
static void
checkZeroPorts(Config const& config)
{
    if (!config.exists(Sections::kServer))
        return;

    for (auto const& name : config.section(Sections::kServer).values())
    {
        if (!config.exists(name))
            return;

        auto const& section = config[name];
        auto const optResult = section.get(Keys::kPort);
        if (optResult)
        {
            auto const port = beast::lexicalCast<std::uint16_t>(*optResult);
            if (port == 0u)
            {
                std::stringstream ss;
                ss << "Invalid value '" << *optResult << "' for key 'port' in [" << name << "]";
                Throw<std::runtime_error>(ss.str());
            }
        }
    }
}

void
Config::load()
{
    // NOTE: this writes to cerr because we want cout to be reserved
    // for the writing of the json response (so that stdout can be part of a
    // pipeline, for instance)
    if (!quiet_)
        std::cerr << "Loading: " << configFile_ << "\n";

    boost::system::error_code ec;
    auto const fileContents = getFileContents(ec, configFile_);

    if (ec)
    {
        std::cerr << "Failed to read '" << configFile_ << "'." << ec.value() << ": " << ec.message()
                  << std::endl;
        return;
    }

    loadFromString(fileContents);
    checkZeroPorts(*this);
}

void
Config::loadFromString(std::string const& fileContents)
{
    IniFileSections secConfig = parseIniFile(fileContents, true);

    build(secConfig);

    if (auto s = getIniFileSection(secConfig, Sections::kIps))
        ips = *s;

    if (auto s = getIniFileSection(secConfig, Sections::kIpsFixed))
        ipsFixed = *s;

    // if the user has specified ip:port then replace : with a space.
    {
        auto replaceColons = [](std::vector<std::string>& strVec) {
            static std::regex const kE(":([0-9]+)$");
            for (auto& line : strVec)
            {
                // skip anything that might be an ipv6 address
                if (std::count(line.begin(), line.end(), ':') != 1)
                    continue;

                std::string const result = std::regex_replace(line, kE, " $1");
                // sanity check the result of the replace, should be same length
                // as input
                if (result.size() == line.size())
                    line = result;
            }
        };

        replaceColons(ipsFixed);
        replaceColons(ips);
    }

    {
        std::string dbPath;
        if (getSingleSection(secConfig, Sections::kDatabasePath, dbPath, j_))
        {
            boost::filesystem::path const p(dbPath);
            legacy(Sections::kDatabasePath, boost::filesystem::absolute(p).string());
        }
    }

    std::string strTemp;

    if (getSingleSection(secConfig, Sections::kNetworkId, strTemp, j_))
    {
        if (strTemp == "main")
        {
            networkId = 0;
        }
        else if (strTemp == "testnet")
        {
            networkId = 1;
        }
        else if (strTemp == "devnet")
        {
            networkId = 2;
        }
        else
        {
            networkId = beast::lexicalCastThrow<uint32_t>(strTemp);
        }
    }

    if (getSingleSection(secConfig, Sections::kPeerPrivate, strTemp, j_))
        peerPrivate = beast::lexicalCastThrow<bool>(strTemp);

    if (getSingleSection(secConfig, Sections::kPeersMax, strTemp, j_))
    {
        peersMax = beast::lexicalCastThrow<std::size_t>(strTemp);
    }
    else
    {
        std::optional<std::size_t> peersInMaxOpt{};
        if (getSingleSection(secConfig, Sections::kPeersInMax, strTemp, j_))
        {
            peersInMaxOpt = beast::lexicalCastThrow<std::size_t>(strTemp);
            if (*peersInMaxOpt > 1000)
            {
                Throw<std::runtime_error>(
                    std::string("Invalid value specified in [") + Sections::kPeersInMax +
                    "] section; the value must be less or equal than 1000");
            }
        }

        std::optional<std::size_t> peersOutMaxOpt{};
        if (getSingleSection(secConfig, Sections::kPeersOutMax, strTemp, j_))
        {
            peersOutMaxOpt = beast::lexicalCastThrow<std::size_t>(strTemp);
            if (*peersOutMaxOpt < 10 || *peersOutMaxOpt > 1000)
            {
                Throw<std::runtime_error>(
                    std::string("Invalid value specified in [") + Sections::kPeersOutMax +
                    "] section; the value must be in range 10-1000");
            }
        }

        // if one section is configured then the other must be configured too
        if ((peersInMaxOpt && !peersOutMaxOpt) || (peersOutMaxOpt && !peersInMaxOpt))
        {
            Throw<std::runtime_error>(
                std::string("Both sections [") + Sections::kPeersInMax + "]" + " and [" +
                Sections::kPeersOutMax + "] must be configured");
        }

        if (peersInMaxOpt && peersOutMaxOpt)
        {
            peersInMax = *peersInMaxOpt;
            peersOutMax = *peersOutMaxOpt;
        }
    }

    if (getSingleSection(secConfig, Sections::kNodeSize, strTemp, j_))
    {
        if (boost::iequals(strTemp, "tiny"))
        {
            nodeSize = 0;
        }
        else if (boost::iequals(strTemp, "small"))
        {
            nodeSize = 1;
        }
        else if (boost::iequals(strTemp, "medium"))
        {
            nodeSize = 2;
        }
        else if (boost::iequals(strTemp, "large"))
        {
            nodeSize = 3;
        }
        else if (boost::iequals(strTemp, "huge"))
        {
            nodeSize = 4;
        }
        else
        {
            nodeSize = std::min<std::size_t>(4, beast::lexicalCastThrow<std::size_t>(strTemp));
        }
    }

    if (getSingleSection(secConfig, Sections::kSigningSupport, strTemp, j_))
        signingEnabled_ = beast::lexicalCastThrow<bool>(strTemp);

    if (getSingleSection(secConfig, Sections::kElbSupport, strTemp, j_))
        elbSupport = beast::lexicalCastThrow<bool>(strTemp);

    getSingleSection(secConfig, Sections::kSslVerifyFile, sslVerifyFile, j_);
    getSingleSection(secConfig, Sections::kSslVerifyDir, sslVerifyDir, j_);

    if (getSingleSection(secConfig, Sections::kSslVerify, strTemp, j_))
        sslVerify = beast::lexicalCastThrow<bool>(strTemp);

    if (getSingleSection(secConfig, Sections::kRelayValidations, strTemp, j_))
    {
        if (boost::iequals(strTemp, "all"))
        {
            relayUntrustedValidations = 1;
        }
        else if (boost::iequals(strTemp, "trusted"))
        {
            relayUntrustedValidations = 0;
        }
        else if (boost::iequals(strTemp, "drop_untrusted"))
        {
            relayUntrustedValidations = -1;
        }
        else
        {
            Throw<std::runtime_error>(
                std::string("Invalid value specified in [") + Sections::kRelayValidations +
                "] section");
        }
    }

    if (getSingleSection(secConfig, Sections::kRelayProposals, strTemp, j_))
    {
        if (boost::iequals(strTemp, "all"))
        {
            relayUntrustedProposals = 1;
        }
        else if (boost::iequals(strTemp, "trusted"))
        {
            relayUntrustedProposals = 0;
        }
        else if (boost::iequals(strTemp, "drop_untrusted"))
        {
            relayUntrustedProposals = -1;
        }
        else
        {
            Throw<std::runtime_error>(
                std::string("Invalid value specified in [") + Sections::kRelayProposals +
                "] section");
        }
    }

    if (exists(Sections::kValidationSeed) && exists(Sections::kValidatorToken))
    {
        Throw<std::runtime_error>(
            std::string("Cannot have both [") + Sections::kValidationSeed + "] and [" +
            Sections::kValidatorToken + "] config sections");
    }

    if (getSingleSection(secConfig, Sections::kNetworkQuorum, strTemp, j_))
        networkQuorum = beast::lexicalCastThrow<std::size_t>(strTemp);

    fees = setupFeeVote(section(Sections::kVoting));
    /* [fee_default] is documented in the example config files as useful for
     * things like offline transaction signing. Until that's completely
     * deprecated, allow it to override the [voting] section. */
    if (getSingleSection(secConfig, Sections::kFeeDefault, strTemp, j_))
        fees.referenceFee = beast::lexicalCastThrow<std::uint64_t>(strTemp);

    if (getSingleSection(secConfig, Sections::kLedgerHistory, strTemp, j_))
    {
        if (boost::iequals(strTemp, "full"))
        {
            ledgerHistory = std::numeric_limits<decltype(ledgerHistory)>::max();
        }
        else if (boost::iequals(strTemp, "none"))
        {
            ledgerHistory = 0;
        }
        else
        {
            ledgerHistory = beast::lexicalCastThrow<std::uint32_t>(strTemp);
        }
    }

    if (getSingleSection(secConfig, Sections::kFetchDepth, strTemp, j_))
    {
        if (boost::iequals(strTemp, "none"))
        {
            fetchDepth = 0;
        }
        else if (boost::iequals(strTemp, "full"))
        {
            fetchDepth = std::numeric_limits<decltype(fetchDepth)>::max();
        }
        else
        {
            fetchDepth = beast::lexicalCastThrow<std::uint32_t>(strTemp);
        }

        fetchDepth = std::max<uint32_t>(fetchDepth, 10);
    }

    // By default, validators don't have pathfinding enabled, unless it is
    // explicitly requested by the server's admin.
    if (exists(Sections::kValidationSeed) || exists(Sections::kValidatorToken))
        pathSearchMax = 0;

    if (getSingleSection(secConfig, Sections::kPathSearchOld, strTemp, j_))
        pathSearchOld = beast::lexicalCastThrow<int>(strTemp);
    if (getSingleSection(secConfig, Sections::kPathSearch, strTemp, j_))
        pathSearch = beast::lexicalCastThrow<int>(strTemp);
    if (getSingleSection(secConfig, Sections::kPathSearchFast, strTemp, j_))
        pathSearchFast = beast::lexicalCastThrow<int>(strTemp);
    if (getSingleSection(secConfig, Sections::kPathSearchMax, strTemp, j_))
        pathSearchMax = beast::lexicalCastThrow<int>(strTemp);

    if (getSingleSection(secConfig, Sections::kDebugLogfile, strTemp, j_))
        debugLogfile_ = strTemp;

    if (getSingleSection(secConfig, Sections::kSweepInterval, strTemp, j_))
    {
        sweepInterval = beast::lexicalCastThrow<std::size_t>(strTemp);

        if (sweepInterval < 10 || sweepInterval > 600)
        {
            Throw<std::runtime_error>(
                std::string("Invalid ") + Sections::kSweepInterval +
                ": must be between 10 and 600 inclusive");
        }
    }

    if (getSingleSection(secConfig, Sections::kWorkers, strTemp, j_))
    {
        workers = beast::lexicalCastThrow<int>(strTemp);

        if (workers < 1 || workers > 1024)
        {
            Throw<std::runtime_error>(
                std::string("Invalid ") + Sections::kWorkers +
                ": must be between 1 and 1024 inclusive.");
        }
    }

    if (getSingleSection(secConfig, Sections::kIoWorkers, strTemp, j_))
    {
        ioWorkers = beast::lexicalCastThrow<int>(strTemp);

        if (ioWorkers < 1 || ioWorkers > 1024)
        {
            Throw<std::runtime_error>(
                std::string("Invalid ") + Sections::kIoWorkers +
                ": must be between 1 and 1024 inclusive.");
        }
    }

    if (getSingleSection(secConfig, Sections::kPrefetchWorkers, strTemp, j_))
    {
        prefetchWorkers = beast::lexicalCastThrow<int>(strTemp);

        if (prefetchWorkers < 1 || prefetchWorkers > 1024)
        {
            Throw<std::runtime_error>(
                std::string("Invalid ") + Sections::kPrefetchWorkers +
                ": must be between 1 and 1024 inclusive.");
        }
    }

    if (getSingleSection(secConfig, Sections::kCompression, strTemp, j_))
        compression = beast::lexicalCastThrow<bool>(strTemp);

    if (getSingleSection(secConfig, Sections::kLedgerReplay, strTemp, j_))
        ledgerReplay = beast::lexicalCastThrow<bool>(strTemp);

    if (exists(Sections::kReduceRelay))
    {
        auto sec = section(Sections::kReduceRelay);

        /////////////////////  !!TEMPORARY CODE BLOCK!! ////////////////////////
        // vp_enable config option is deprecated by vp_base_squelch_enable    //
        // This option is kept for backwards compatibility. When squelching   //
        // is the default algorithm, it must be replaced with:                //
        //  VP_REDUCE_RELAY_BASE_SQUELCH_ENABLE =                             //
        //  sec.value_or("vp_base_squelch_enable", true);                     //
        if (sec.exists(Keys::kVpBaseSquelchEnable) && sec.exists(Keys::kVpEnable))
        {
            Throw<std::runtime_error>(
                std::string("Invalid ") + Sections::kReduceRelay +
                " cannot specify both vp_base_squelch_enable and vp_enable "
                "options. "
                "vp_enable was deprecated and replaced by "
                "vp_base_squelch_enable");
        }

        if (sec.exists(Keys::kVpBaseSquelchEnable))
        {
            vpReduceRelayBaseSquelchEnable = sec.valueOr(Keys::kVpBaseSquelchEnable, false);
        }
        else if (sec.exists(Keys::kVpEnable))
        {
            vpReduceRelayBaseSquelchEnable = sec.valueOr(Keys::kVpEnable, false);
        }
        else
        {
            vpReduceRelayBaseSquelchEnable = false;
        }
        /////////////////  !!END OF TEMPORARY CODE BLOCK!! /////////////////////

        /////////////////////  !!TEMPORARY CODE BLOCK!! ///////////////////////
        // Temporary squelching config for the peers selected as a source of //
        // validator messages. The config must be removed once squelching is //
        // made the default routing algorithm.                               //
        vpReduceRelaySquelchMaxSelectedPeers = sec.valueOr(Keys::kVpBaseSquelchMaxSelectedPeers, 5);
        if (vpReduceRelaySquelchMaxSelectedPeers < 3)
        {
            Throw<std::runtime_error>(
                std::string("Invalid ") + Sections::kReduceRelay +
                " vp_base_squelch_max_selected_peers must be "
                "greater than or equal to 3");
        }
        /////////////////  !!END OF TEMPORARY CODE BLOCK!! /////////////////////

        txReduceRelayEnable = sec.valueOr(Keys::kTxEnable, false);
        txReduceRelayMetrics = sec.valueOr(Keys::kTxMetrics, false);
        txReduceRelayMinPeers = sec.valueOr(Keys::kTxMinPeers, 20);
        txRelayPercentage = sec.valueOr(Keys::kTxRelayPercentage, 25);
        if (txRelayPercentage < 10 || txRelayPercentage > 100 || txReduceRelayMinPeers < 10)
        {
            Throw<std::runtime_error>(
                std::string("Invalid ") + Sections::kReduceRelay +
                ", tx_min_peers must be greater than or equal to 10"
                ", tx_relay_percentage must be greater than or equal to 10 "
                "and less than or equal to 100");
        }
    }

    if (getSingleSection(secConfig, Sections::kMaxTransactions, strTemp, j_))
    {
        maxTransactions =
            std::clamp(beast::lexicalCastThrow<int>(strTemp), kMinJobQueueTx, kMaxJobQueueTx);
    }

    if (getSingleSection(secConfig, Sections::kServerDomain, strTemp, j_))
    {
        if (!isProperlyFormedTomlDomain(strTemp))
        {
            Throw<std::runtime_error>(
                std::string("Invalid ") + Sections::kServerDomain +
                ": the domain name does not appear to meet the requirements.");
        }

        serverDomain = strTemp;
    }

    if (exists(Sections::kOverlay))
    {
        auto const sec = section(Sections::kOverlay);

        using namespace std::chrono;

        try
        {
            if (auto val = sec.get(Keys::kMaxUnknownTime))
                maxUnknownTime = seconds{beast::lexicalCastThrow<std::uint32_t>(*val)};
        }
        catch (...)
        {
            Throw<std::runtime_error>(
                std::string("Invalid value 'max_unknown_time' in ") + Sections::kOverlay +
                ": must be of the form '<number>' representing seconds.");
        }

        if (maxUnknownTime < seconds{300} || maxUnknownTime > seconds{1800})
        {
            Throw<std::runtime_error>(
                std::string("Invalid value 'max_unknown_time' in ") + Sections::kOverlay +
                ": the time must be between 300 and 1800 seconds, inclusive.");
        }

        try
        {
            if (auto val = sec.get(Keys::kMaxDivergedTime))
                maxDivergedTime = seconds{beast::lexicalCastThrow<std::uint32_t>(*val)};
        }
        catch (...)
        {
            Throw<std::runtime_error>(
                std::string("Invalid value 'max_diverged_time' in ") + Sections::kOverlay +
                ": must be of the form '<number>' representing seconds.");
        }

        if (maxDivergedTime < seconds{60} || maxDivergedTime > seconds{900})
        {
            Throw<std::runtime_error>(
                std::string("Invalid value 'max_diverged_time' in ") + Sections::kOverlay +
                ": the time must be between 60 and 900 seconds, inclusive.");
        }
    }

    if (getSingleSection(secConfig, Sections::kAmendmentMajorityTime, strTemp, j_))
    {
        using namespace std::chrono;
        boost::regex const re("^\\s*(\\d+)\\s*(minutes|hours|days|weeks)\\s*(\\s+.*)?$");
        boost::smatch match;
        if (!boost::regex_match(strTemp, match, re))
        {
            Throw<std::runtime_error>(
                std::string("Invalid ") + Sections::kAmendmentMajorityTime +
                ", must be: [0-9]+ [minutes|hours|days|weeks]");
        }

        std::uint32_t const duration = beast::lexicalCastThrow<std::uint32_t>(match[1].str());

        if (boost::iequals(match[2], "minutes"))
        {
            amendmentMajorityTime = minutes(duration);
        }
        else if (boost::iequals(match[2], "hours"))
        {
            amendmentMajorityTime = hours(duration);
        }
        else if (boost::iequals(match[2], "days"))
        {
            amendmentMajorityTime = days(duration);
        }
        else if (boost::iequals(match[2], "weeks"))
        {
            amendmentMajorityTime = weeks(duration);
        }

        if (amendmentMajorityTime < minutes(15))
        {
            Throw<std::runtime_error>(
                std::string("Invalid ") + Sections::kAmendmentMajorityTime +
                ", the minimum amount of time an amendment must hold a "
                "majority is 15 minutes");
        }
    }

    if (getSingleSection(secConfig, Sections::kBetaRpcApi, strTemp, j_))
        betaRpcApi = beast::lexicalCastThrow<bool>(strTemp);

    // Do not load trusted validator configuration for standalone mode
    if (!runStandalone_)
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

        if (getSingleSection(secConfig, Sections::kValidatorsFile, strTemp, j_))
        {
            validatorsFile = strTemp;

            if (validatorsFile.empty())
            {
                Throw<std::runtime_error>(
                    std::string("Invalid path specified in [") + Sections::kValidatorsFile + "]");
            }

            if (!validatorsFile.is_absolute() && !configDir.empty())
                validatorsFile = configDir / validatorsFile;

            if (!boost::filesystem::exists(validatorsFile))
            {
                Throw<std::runtime_error>(
                    std::string("The file specified in [") + Sections::kValidatorsFile +
                    "] "
                    "does not exist: " +
                    validatorsFile.string());
            }
            else if (
                !boost::filesystem::is_regular_file(validatorsFile) &&
                !boost::filesystem::is_symlink(validatorsFile))
            {
                Throw<std::runtime_error>(
                    std::string("Invalid file specified in [") + Sections::kValidatorsFile +
                    "]: " + validatorsFile.string());
            }
        }
        else if (!configDir.empty())
        {
            validatorsFile = configDir / kValidatorsFileName;

            if (!validatorsFile.empty())
            {
                if (!boost::filesystem::exists(validatorsFile))
                {
                    validatorsFile.clear();
                }
                else if (
                    !boost::filesystem::is_regular_file(validatorsFile) &&
                    !boost::filesystem::is_symlink(validatorsFile))
                {
                    validatorsFile.clear();
                }
            }
        }

        if (!validatorsFile.empty() && boost::filesystem::exists(validatorsFile) &&
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

            auto entries = getIniFileSection(iniFile, Sections::kValidators);

            if (entries != nullptr)
                section(Sections::kValidators).append(*entries);

            auto valKeyEntries = getIniFileSection(iniFile, Sections::kValidatorKeys);

            if (valKeyEntries != nullptr)
                section(Sections::kValidatorKeys).append(*valKeyEntries);

            auto valSiteEntries = getIniFileSection(iniFile, Sections::kValidatorListSites);

            if (valSiteEntries != nullptr)
                section(Sections::kValidatorListSites).append(*valSiteEntries);

            auto valListKeys = getIniFileSection(iniFile, Sections::kValidatorListKeys);

            if (valListKeys != nullptr)
                section(Sections::kValidatorListKeys).append(*valListKeys);

            auto valListThreshold = getIniFileSection(iniFile, Sections::kValidatorListThreshold);

            if (valListThreshold != nullptr)
                section(Sections::kValidatorListThreshold).append(*valListThreshold);

            if ((entries == nullptr) && (valKeyEntries == nullptr) && (valListKeys == nullptr))
            {
                Throw<std::runtime_error>(
                    std::string("The file specified in [") + Sections::kValidatorsFile +
                    "] "
                    "does not contain a [" +
                    Sections::kValidators +
                    "], "
                    "[" +
                    Sections::kValidatorKeys +
                    "] or "
                    "[" +
                    Sections::kValidatorListKeys +
                    "]"
                    " section: " +
                    validatorsFile.string());
            }
        }

        validatorListThreshold = [&]() -> std::optional<std::size_t> {
            auto const& listThreshold = section(Sections::kValidatorListThreshold);
            if (listThreshold.lines().empty())
            {
                return std::nullopt;
            }
            if (listThreshold.values().size() == 1)
            {
                auto strTemp = listThreshold.values()[0];
                auto const listThreshold = beast::lexicalCastThrow<std::size_t>(strTemp);
                if (listThreshold == 0)
                {
                    return std::nullopt;  // NOTE: Explicitly ask for computed
                }
                if (listThreshold > section(Sections::kValidatorListKeys).values().size())
                {
                    Throw<std::runtime_error>(
                        std::string(
                            "Value in config section "
                            "[") +
                        Sections::kValidatorListThreshold +
                        "] exceeds the number of configured list keys");
                }
                return listThreshold;
            }

            Throw<std::runtime_error>(
                std::string(
                    "Config section "
                    "[") +
                Sections::kValidatorListThreshold + "] should contain single value only");
        }();

        // Consolidate [validator_keys] and [validators]
        section(Sections::kValidators).append(section(Sections::kValidatorKeys).lines());

        if (!section(Sections::kValidatorListSites).lines().empty() &&
            section(Sections::kValidatorListKeys).lines().empty())
        {
            Throw<std::runtime_error>(
                "[" + std::string(Sections::kValidatorListKeys) + "] config section is missing");
        }
    }

    {
        auto const part = section(Sections::kFeatures);
        for (auto const& s : part.values())
        {
            if (auto const f = getRegisteredFeature(s))
            {
                features.insert(*f);
            }
            else
            {
                Throw<std::runtime_error>("Unknown feature: " + s + "  in config file.");
            }
        }
    }

    // This doesn't properly belong here, but check to make sure that the
    // value specified for network_quorum is achievable:
    {
        auto pm = peersMax;

        // FIXME this apparently magic value is actually defined as a constant
        //       elsewhere (see defaultMaxPeers) but we handle this check here.
        if (pm == 0)
            pm = 21;

        if (networkQuorum > pm)
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
    auto logFile = debugLogfile_;

    if (!logFile.empty() && !logFile.is_absolute())
    {
        // Unless an absolute path for the log file is specified, the
        // path is relative to the config file directory.
        logFile = boost::filesystem::absolute(logFile, configDir);
    }

    if (!logFile.empty())
    {
        auto logDir = logFile.parent_path();

        if (!boost::filesystem::is_directory(logDir))
        {
            boost::system::error_code ec;
            boost::filesystem::create_directories(logDir, ec);

            // If we fail, we warn but continue so that the calling code can
            // decide how to handle this situation.
            if (ec)
            {
                std::cerr << "Unable to create log file path " << logDir << ": " << ec.message()
                          << '\n';
            }
        }
    }

    return logFile;
}

int
Config::getValueFor(SizedItem item, std::optional<std::size_t> node) const
{
    auto const index = static_cast<std::underlying_type_t<SizedItem>>(item);
    XRPL_ASSERT(index < kSizedItems.size(), "xrpl::Config::getValueFor : valid index input");
    XRPL_ASSERT(!node || *node <= 4, "xrpl::Config::getValueFor : unset or valid node");
    return kSizedItems.at(index).second.at(node.value_or(nodeSize));
}

FeeSetup
setupFeeVote(Section const& section)
{
    FeeSetup setup;
    {
        std::uint64_t temp = 0;
        if (set(temp, Keys::kReferenceFee, section) &&
            temp <= std::numeric_limits<XRPAmount::value_type>::max())
            setup.referenceFee = temp;
    }
    {
        std::uint32_t temp = 0;
        if (set(temp, Keys::kAccountReserve, section))
            setup.accountReserve = temp;
        if (set(temp, Keys::kOwnerReserve, section))
            setup.ownerReserve = temp;
    }
    return setup;
}

DatabaseCon::Setup
setupDatabaseCon(Config const& c, std::optional<beast::Journal> j)
{
    DatabaseCon::Setup setup;

    setup.startUp = c.startUp;
    setup.standAlone = c.standalone();
    setup.dataDir = c.legacy(Sections::kDatabasePath);
    if (!setup.standAlone && setup.dataDir.empty())
    {
        Throw<std::runtime_error>("database_path must be set.");
    }

    if (!setup.globalPragma)
    {
        auto const& sqlite = c.section(Sections::kSqlite);
        auto result = std::make_unique<std::vector<std::string>>();
        result->reserve(3);

        // defaults
        std::string safetyLevel;
        std::string journalMode = "wal";
        std::string synchronous = "normal";
        std::string tempStore = "file";
        bool showRiskWarning = false;

        if (set(safetyLevel, "safety_level", sqlite))
        {
            if (boost::iequals(safetyLevel, "low"))
            {
                // low safety defaults
                journalMode = "memory";
                synchronous = "off";
                tempStore = "memory";
                showRiskWarning = true;
            }
            else if (!boost::iequals(safetyLevel, "high"))
            {
                Throw<std::runtime_error>("Invalid safety_level value: " + safetyLevel);
            }
        }

        {
            // #journal_mode Valid values : delete, truncate, persist,
            // memory, wal, off
            if (set(journalMode, "journal_mode", sqlite) && !safetyLevel.empty())
            {
                Throw<std::runtime_error>(
                    "Configuration file may not define both "
                    "\"safety_level\" and \"journal_mode\"");
            }
            bool const higherRisk =
                boost::iequals(journalMode, "memory") || boost::iequals(journalMode, "off");
            showRiskWarning = showRiskWarning || higherRisk;
            if (higherRisk || boost::iequals(journalMode, "delete") ||
                boost::iequals(journalMode, "truncate") || boost::iequals(journalMode, "persist") ||
                boost::iequals(journalMode, "wal"))
            {
                result->emplace_back(
                    boost::str(boost::format(kCommonDbPragmaJournal) % journalMode));
            }
            else
            {
                Throw<std::runtime_error>("Invalid journal_mode value: " + journalMode);
            }
        }

        {
            // #synchronous Valid values : off, normal, full, extra
            if (set(synchronous, "synchronous", sqlite) && !safetyLevel.empty())
            {
                Throw<std::runtime_error>(
                    "Configuration file may not define both "
                    "\"safety_level\" and \"synchronous\"");
            }
            bool const higherRisk = boost::iequals(synchronous, "off");
            showRiskWarning = showRiskWarning || higherRisk;
            if (higherRisk || boost::iequals(synchronous, "normal") ||
                boost::iequals(synchronous, "full") || boost::iequals(synchronous, "extra"))
            {
                result->emplace_back(boost::str(boost::format(kCommonDbPragmaSync) % synchronous));
            }
            else
            {
                Throw<std::runtime_error>("Invalid synchronous value: " + synchronous);
            }
        }

        {
            // #temp_store Valid values : default, file, memory
            if (set(tempStore, "temp_store", sqlite) && !safetyLevel.empty())
            {
                Throw<std::runtime_error>(
                    "Configuration file may not define both "
                    "\"safety_level\" and \"temp_store\"");
            }
            bool const higherRisk = boost::iequals(tempStore, "memory");
            showRiskWarning = showRiskWarning || higherRisk;
            if (higherRisk || boost::iequals(tempStore, "default") ||
                boost::iequals(tempStore, "file"))
            {
                result->emplace_back(boost::str(boost::format(kCommonDbPragmaTemp) % tempStore));
            }
            else
            {
                Throw<std::runtime_error>("Invalid temp_store value: " + tempStore);
            }
        }

        if (showRiskWarning && j && c.ledgerHistory > kSqliteTuningCutoff)
        {
            JLOG(j->warn()) << "reducing the data integrity guarantees from the "
                               "default [sqlite] behavior is not recommended for "
                               "nodes storing large amounts of history, because of the "
                               "difficulty inherent in rebuilding corrupted data.";
        }
        XRPL_ASSERT(
            result->size() == 3, "xrpl::setup_DatabaseCon::globalPragma : result size is 3");
        setup.globalPragma = std::move(result);
    }
    setup.useGlobalPragma = true;

    auto setPragma = [](std::string& pragma, std::string const& key, int64_t value) {
        pragma = "PRAGMA " + key + "=" + std::to_string(value) + ";";
    };

    // Lgr Pragma
    setPragma(setup.lgrPragma[0], "journal_size_limit", 1582080);

    // TX Pragma
    int64_t pageSize = 4096;
    int64_t journalSizeLimit = 1582080;
    if (c.exists(Sections::kSqlite))
    {
        auto& s = c.section(Sections::kSqlite);
        set(journalSizeLimit, Keys::kJournalSizeLimit, s);
        set(pageSize, Keys::kPageSize, s);
        if (pageSize < 512 || pageSize > 65536)
            Throw<std::runtime_error>("Invalid page_size. Must be between 512 and 65536.");

        if ((pageSize & (pageSize - 1)) != 0)
            Throw<std::runtime_error>("Invalid page_size. Must be a power of 2.");
    }

    setPragma(setup.txPragma[0], "page_size", pageSize);
    setPragma(setup.txPragma[1], "journal_size_limit", journalSizeLimit);
    setPragma(setup.txPragma[2], "max_page_count", 4294967294);
    setPragma(setup.txPragma[3], "mmap_size", 17179869184);

    return setup;
}
}  // namespace xrpl
