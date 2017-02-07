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

#include <BeastConfig.h>
#include <ripple/core/Config.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/json/json_reader.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/SystemParameters.h>
#include <ripple/net/HTTPClient.h>
#include <ripple/beast/core/LexicalCast.h>
#include <beast/core/detail/ci_char_traits.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/regex.hpp>
#include <fstream>
#include <iostream>
#include <iterator>

namespace ripple {

//
// TODO: Check permissions on config file before using it.
//

#define SECTION_DEFAULT_NAME    ""

IniFileSections
parseIniFile (std::string const& strInput, const bool bTrim)
{
    std::string strData (strInput);
    std::vector<std::string> vLines;
    IniFileSections secResult;

    // Convert DOS format to unix.
    boost::algorithm::replace_all (strData, "\r\n", "\n");

    // Convert MacOS format to unix.
    boost::algorithm::replace_all (strData, "\r", "\n");

    boost::algorithm::split (vLines, strData,
        boost::algorithm::is_any_of ("\n"));

    // Set the default Section name.
    std::string strSection  = SECTION_DEFAULT_NAME;

    // Initialize the default Section.
    secResult[strSection]   = IniFileSections::mapped_type ();

    // Parse each line.
    for (auto& strValue : vLines)
    {
        if (strValue.empty () || strValue[0] == '#')
        {
            // Blank line or comment, do nothing.
        }
        else if (strValue[0] == '[' && strValue[strValue.length () - 1] == ']')
        {
            // New Section.
            strSection              = strValue.substr (1, strValue.length () - 2);
            secResult.emplace(strSection, IniFileSections::mapped_type{});
        }
        else
        {
            // Another line for Section.
            if (bTrim)
                boost::algorithm::trim (strValue);

            if (!strValue.empty ())
                secResult[strSection].push_back (strValue);
        }
    }

    return secResult;
}

IniFileSections::mapped_type*
getIniFileSection (IniFileSections& secSource, std::string const& strSection)
{
    IniFileSections::iterator it;
    IniFileSections::mapped_type* smtResult;
    it  = secSource.find (strSection);
    if (it == secSource.end ())
        smtResult   = 0;
    else
        smtResult   = & (it->second);
    return smtResult;
}

bool getSingleSection (IniFileSections& secSource,
    std::string const& strSection, std::string& strValue, beast::Journal j)
{
    IniFileSections::mapped_type* pmtEntries =
        getIniFileSection (secSource, strSection);
    bool bSingle = pmtEntries && 1 == pmtEntries->size ();

    if (bSingle)
    {
        strValue    = (*pmtEntries)[0];
    }
    else if (pmtEntries)
    {
        JLOG (j.warn()) << boost::str (
            boost::format ("Section [%s]: requires 1 line not %d lines.") %
            strSection % pmtEntries->size ());
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

static
std::string
getEnvVar (char const* name)
{
    std::string value;

    auto const v = getenv (name);

    if (v != nullptr)
        value = v;

    return value;
}

void Config::setupControl(bool bQuiet,
    bool bSilent, bool bStandalone)
{
    QUIET = bQuiet || bSilent;
    SILENT = bSilent;
    RUN_STANDALONE = bStandalone;
}

void Config::setup (std::string const& strConf, bool bQuiet,
    bool bSilent, bool bStandalone)
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

    if (!strConf.empty ())
    {
        // --conf=<path> : everything is relative that file.
        CONFIG_FILE             = strConfFile;
        CONFIG_DIR              = boost::filesystem::absolute (CONFIG_FILE);
        CONFIG_DIR.remove_filename ();
        dataDir                 = CONFIG_DIR / strDbPath;
    }
    else
    {
        CONFIG_DIR              = boost::filesystem::current_path ();
        CONFIG_FILE             = CONFIG_DIR / strConfFile;
        dataDir                 = CONFIG_DIR / strDbPath;

        // Construct XDG config and data home.
        // http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html
        std::string strHome          = getEnvVar ("HOME");
        std::string strXdgConfigHome = getEnvVar ("XDG_CONFIG_HOME");
        std::string strXdgDataHome   = getEnvVar ("XDG_DATA_HOME");

        if (boost::filesystem::exists (CONFIG_FILE)
                // Can we figure out XDG dirs?
                || (strHome.empty () && (strXdgConfigHome.empty () || strXdgDataHome.empty ())))
        {
            // Current working directory is fine, put dbs in a subdir.
        }
        else
        {
            if (strXdgConfigHome.empty ())
            {
                // $XDG_CONFIG_HOME was not set, use default based on $HOME.
                strXdgConfigHome    = strHome + "/.config";
            }

            if (strXdgDataHome.empty ())
            {
                // $XDG_DATA_HOME was not set, use default based on $HOME.
                strXdgDataHome  = strHome + "/.local/share";
            }

            CONFIG_DIR  = strXdgConfigHome + "/" + systemName ();
            CONFIG_FILE = CONFIG_DIR / strConfFile;
            dataDir    = strXdgDataHome + "/" + systemName ();

            if (!boost::filesystem::exists (CONFIG_FILE))
            {
                CONFIG_DIR  = "/etc/opt/" + systemName ();
                CONFIG_FILE = CONFIG_DIR / strConfFile;
                dataDir = "/var/opt/" + systemName();
            }
        }
    }

    // Update default values
    load ();
    {
        // load() may have set a new value for the dataDir
        std::string const dbPath (legacy ("database_path"));
        if (!dbPath.empty ())
            dataDir = boost::filesystem::path (dbPath);
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

    HTTPClient::initializeSSLContext(*this);

    if (RUN_STANDALONE)
        LEDGER_HISTORY = 0;
}

void Config::load ()
{
    // NOTE: this writes to cerr because we want cout to be reserved
    // for the writing of the json response (so that stdout can be part of a
    // pipeline, for instance)
    if (!QUIET)
        std::cerr << "Loading: " << CONFIG_FILE << "\n";

    std::ifstream ifsConfig (CONFIG_FILE.c_str (), std::ios::in);

    if (!ifsConfig)
    {
        std::cerr << "Failed to open '" << CONFIG_FILE << "'." << std::endl;
        return;
    }

    std::string fileContents;
    fileContents.assign ((std::istreambuf_iterator<char>(ifsConfig)),
                          std::istreambuf_iterator<char>());

    if (ifsConfig.bad ())
    {
        std::cerr << "Failed to read '" << CONFIG_FILE << "'." << std::endl;
        return;
    }

    loadFromString (fileContents);
}

void Config::loadFromString (std::string const& fileContents)
{
    IniFileSections secConfig = parseIniFile (fileContents, true);

    build (secConfig);

    if (auto s = getIniFileSection (secConfig, SECTION_IPS))
        IPS = *s;

    if (auto s = getIniFileSection (secConfig, SECTION_IPS_FIXED))
        IPS_FIXED = *s;

    if (auto s = getIniFileSection (secConfig, SECTION_SNTP))
        SNTP_SERVERS = *s;

    {
        std::string dbPath;
        if (getSingleSection (secConfig, "database_path", dbPath, j_))
        {
            boost::filesystem::path p(dbPath);
            legacy("database_path",
                   boost::filesystem::absolute (p).string ());
        }
    }

    std::string strTemp;

    if (getSingleSection (secConfig, SECTION_PEER_PRIVATE, strTemp, j_))
        PEER_PRIVATE = beast::lexicalCastThrow <bool> (strTemp);

    if (getSingleSection (secConfig, SECTION_PEERS_MAX, strTemp, j_))
        PEERS_MAX = std::max (0, beast::lexicalCastThrow <int> (strTemp));

    if (getSingleSection (secConfig, SECTION_NODE_SIZE, strTemp, j_))
    {
        if (beast::detail::ci_equal(strTemp, "tiny"))
            NODE_SIZE = 0;
        else if (beast::detail::ci_equal(strTemp, "small"))
            NODE_SIZE = 1;
        else if (beast::detail::ci_equal(strTemp, "medium"))
            NODE_SIZE = 2;
        else if (beast::detail::ci_equal(strTemp, "large"))
            NODE_SIZE = 3;
        else if (beast::detail::ci_equal(strTemp, "huge"))
            NODE_SIZE = 4;
        else
        {
            NODE_SIZE = beast::lexicalCastThrow <int> (strTemp);

            if (NODE_SIZE < 0)
                NODE_SIZE = 0;
            else if (NODE_SIZE > 4)
                NODE_SIZE = 4;
        }
    }

    if (getSingleSection (secConfig, SECTION_ELB_SUPPORT, strTemp, j_))
        ELB_SUPPORT         = beast::lexicalCastThrow <bool> (strTemp);

    if (getSingleSection (secConfig, SECTION_WEBSOCKET_PING_FREQ, strTemp, j_))
        WEBSOCKET_PING_FREQ = std::chrono::seconds{beast::lexicalCastThrow <int>(strTemp)};

    getSingleSection (secConfig, SECTION_SSL_VERIFY_FILE, SSL_VERIFY_FILE, j_);
    getSingleSection (secConfig, SECTION_SSL_VERIFY_DIR, SSL_VERIFY_DIR, j_);

    if (getSingleSection (secConfig, SECTION_SSL_VERIFY, strTemp, j_))
        SSL_VERIFY          = beast::lexicalCastThrow <bool> (strTemp);

    if (exists(SECTION_VALIDATION_SEED) && exists(SECTION_VALIDATOR_TOKEN))
        Throw<std::runtime_error> (
            "Cannot have both [" SECTION_VALIDATION_SEED "] "
            "and [" SECTION_VALIDATOR_TOKEN "] config sections");

    if (getSingleSection (secConfig, SECTION_NETWORK_QUORUM, strTemp, j_))
        NETWORK_QUORUM      = beast::lexicalCastThrow <std::size_t> (strTemp);

    if (getSingleSection (secConfig, SECTION_FEE_ACCOUNT_RESERVE, strTemp, j_))
        FEE_ACCOUNT_RESERVE = beast::lexicalCastThrow <std::uint64_t> (strTemp);

    if (getSingleSection (secConfig, SECTION_FEE_OWNER_RESERVE, strTemp, j_))
        FEE_OWNER_RESERVE   = beast::lexicalCastThrow <std::uint64_t> (strTemp);

    if (getSingleSection (secConfig, SECTION_FEE_OFFER, strTemp, j_))
        FEE_OFFER           = beast::lexicalCastThrow <int> (strTemp);

    if (getSingleSection (secConfig, SECTION_FEE_DEFAULT, strTemp, j_))
        FEE_DEFAULT         = beast::lexicalCastThrow <int> (strTemp);

    if (getSingleSection (secConfig, SECTION_LEDGER_HISTORY, strTemp, j_))
    {
        if (beast::detail::ci_equal(strTemp, "full"))
            LEDGER_HISTORY = 1000000000u;
        else if (beast::detail::ci_equal(strTemp, "none"))
            LEDGER_HISTORY = 0;
        else
            LEDGER_HISTORY = beast::lexicalCastThrow <std::uint32_t> (strTemp);
    }

    if (getSingleSection (secConfig, SECTION_FETCH_DEPTH, strTemp, j_))
    {
        if (beast::detail::ci_equal(strTemp, "none"))
            FETCH_DEPTH = 0;
        else if (beast::detail::ci_equal(strTemp, "full"))
            FETCH_DEPTH = 1000000000u;
        else
            FETCH_DEPTH = beast::lexicalCastThrow <std::uint32_t> (strTemp);

        if (FETCH_DEPTH < 10)
            FETCH_DEPTH = 10;
    }

    if (getSingleSection (secConfig, SECTION_PATH_SEARCH_OLD, strTemp, j_))
        PATH_SEARCH_OLD     = beast::lexicalCastThrow <int> (strTemp);
    if (getSingleSection (secConfig, SECTION_PATH_SEARCH, strTemp, j_))
        PATH_SEARCH         = beast::lexicalCastThrow <int> (strTemp);
    if (getSingleSection (secConfig, SECTION_PATH_SEARCH_FAST, strTemp, j_))
        PATH_SEARCH_FAST    = beast::lexicalCastThrow <int> (strTemp);
    if (getSingleSection (secConfig, SECTION_PATH_SEARCH_MAX, strTemp, j_))
        PATH_SEARCH_MAX     = beast::lexicalCastThrow <int> (strTemp);

    if (getSingleSection (secConfig, SECTION_DEBUG_LOGFILE, strTemp, j_))
        DEBUG_LOGFILE       = strTemp;

    // Do not load trusted validator configuration for standalone mode
    if (! RUN_STANDALONE)
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

        if (getSingleSection (secConfig, SECTION_VALIDATORS_FILE, strTemp, j_))
        {
            validatorsFile = strTemp;

            if (validatorsFile.empty ())
                Throw<std::runtime_error> (
                    "Invalid path specified in [" SECTION_VALIDATORS_FILE "]");

            if (!validatorsFile.is_absolute() && !CONFIG_DIR.empty())
                validatorsFile = CONFIG_DIR / validatorsFile;

            if (!boost::filesystem::exists (validatorsFile))
                Throw<std::runtime_error> (
                    "The file specified in [" SECTION_VALIDATORS_FILE "] "
                    "does not exist: " + validatorsFile.string());

            else if (!boost::filesystem::is_regular_file (validatorsFile) &&
                    !boost::filesystem::is_symlink (validatorsFile))
                Throw<std::runtime_error> (
                    "Invalid file specified in [" SECTION_VALIDATORS_FILE "]: " +
                    validatorsFile.string());
        }
        else if (!CONFIG_DIR.empty())
        {
            validatorsFile = CONFIG_DIR / validatorsFileName;

            if (!validatorsFile.empty ())
            {
                if(!boost::filesystem::exists (validatorsFile))
                    validatorsFile.clear();
                else if (!boost::filesystem::is_regular_file (validatorsFile) &&
                        !boost::filesystem::is_symlink (validatorsFile))
                    validatorsFile.clear();
            }
        }

        if (!validatorsFile.empty () &&
                boost::filesystem::exists (validatorsFile) &&
                (boost::filesystem::is_regular_file (validatorsFile) ||
                boost::filesystem::is_symlink (validatorsFile)))
        {
            std::ifstream ifsDefault (validatorsFile.native().c_str());

            std::string data;

            data.assign (
                std::istreambuf_iterator<char>(ifsDefault),
                std::istreambuf_iterator<char>());

            auto iniFile = parseIniFile (data, true);

            auto entries = getIniFileSection (
                iniFile,
                SECTION_VALIDATORS);

            if (entries)
                section (SECTION_VALIDATORS).append (*entries);

            auto valKeyEntries = getIniFileSection(
                iniFile,
                SECTION_VALIDATOR_KEYS);

            if (valKeyEntries)
                section (SECTION_VALIDATOR_KEYS).append (*valKeyEntries);

            auto valSiteEntries = getIniFileSection(
                iniFile,
                SECTION_VALIDATOR_LIST_SITES);

            if (valSiteEntries)
                section (SECTION_VALIDATOR_LIST_SITES).append (*valSiteEntries);

            auto valListKeys = getIniFileSection(
                iniFile,
                SECTION_VALIDATOR_LIST_KEYS);

            if (valListKeys)
                section (SECTION_VALIDATOR_LIST_KEYS).append (*valListKeys);

            if (!entries && !valKeyEntries && !valListKeys)
                Throw<std::runtime_error> (
                    "The file specified in [" SECTION_VALIDATORS_FILE "] "
                    "does not contain a [" SECTION_VALIDATORS "], "
                    "[" SECTION_VALIDATOR_KEYS "] or "
                    "[" SECTION_VALIDATOR_LIST_KEYS "]"
                    " section: " +
                    validatorsFile.string());
        }

        // Consolidate [validator_keys] and [validators]
        section (SECTION_VALIDATORS).append (
            section (SECTION_VALIDATOR_KEYS).lines ());

        if (! section (SECTION_VALIDATOR_LIST_SITES).lines().empty() &&
            section (SECTION_VALIDATOR_LIST_KEYS).lines().empty())
        {
            Throw<std::runtime_error> (
                "[" + std::string(SECTION_VALIDATOR_LIST_KEYS) +
                "] config section is missing");
        }
    }

    {
        auto const part = section("features");
        for(auto const& s : part.values())
            features.insert(feature(s));
    }
}

int Config::getSize (SizedItemName item) const
{
    SizedItem sizeTable[] =   //    tiny    small   medium  large       huge
    {

        { siSweepInterval,      {   10,     30,     60,     90,         120     } },

        { siLedgerFetch,        {   2,      2,      3,      3,          3       } },

        { siNodeCacheSize,      {   16384,  32768,  131072, 262144,     524288  } },
        { siNodeCacheAge,       {   60,     90,     120,    900,        1800    } },

        { siTreeCacheSize,      {   128000, 256000, 512000, 768000,     2048000 } },
        { siTreeCacheAge,       {   30,     60,     90,     120,        900     } },

        { siSLECacheSize,       {   4096,   8192,   16384,  65536,      131072  } },
        { siSLECacheAge,        {   30,     60,     90,     120,        300     } },

        { siLedgerSize,         {   32,     128,    256,    384,        768     } },
        { siLedgerAge,          {   30,     90,     180,    240,        900     } },

        { siHashNodeDBCache,    {   4,      12,     24,     64,         128      } },
        { siTxnDBCache,         {   4,      12,     24,     64,         128      } },
        { siLgrDBCache,         {   4,      8,      16,     32,         128      } },
    };

    for (int i = 0; i < (sizeof (sizeTable) / sizeof (SizedItem)); ++i)
    {
        if (sizeTable[i].item == item)
            return sizeTable[i].sizes[NODE_SIZE];
    }

    assert (false);
    return -1;
}

boost::filesystem::path Config::getDebugLogFile () const
{
    auto log_file = DEBUG_LOGFILE;

    if (!log_file.empty () && !log_file.is_absolute ())
    {
        // Unless an absolute path for the log file is specified, the
        // path is relative to the config file directory.
        log_file = boost::filesystem::absolute (
            log_file, CONFIG_DIR);
    }

    if (!log_file.empty ())
    {
        auto log_dir = log_file.parent_path ();

        if (!boost::filesystem::is_directory (log_dir))
        {
            boost::system::error_code ec;
            boost::filesystem::create_directories (log_dir, ec);

            // If we fail, we warn but continue so that the calling code can
            // decide how to handle this situation.
            if (ec)
            {
                std::cerr <<
                    "Unable to create log file path " << log_dir <<
                    ": " << ec.message() << '\n';
            }
        }
    }

    return log_file;
}

} // ripple
