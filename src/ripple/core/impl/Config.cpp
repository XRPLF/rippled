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
#include <ripple/basics/Log.h>
#include <ripple/json/json_reader.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/SystemParameters.h>
#include <ripple/net/HTTPClient.h>
#include <beast/http/URL.h>
#include <beast/module/core/text/LexicalCast.h>
#include <beast/streams/debug_ostream.h>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/regex.hpp>
#include <fstream>
#include <iostream>

#ifndef DUMP_CONFIG
#define DUMP_CONFIG 0
#endif

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

int
countSectionEntries (IniFileSections& secSource, std::string const& strSection)
{
    IniFileSections::mapped_type* pmtEntries =
        getIniFileSection (secSource, strSection);

    return pmtEntries ? pmtEntries->size () : 0;
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
        JLOG (j.warning) << boost::str (
            boost::format ("Section [%s]: requires 1 line not %d lines.") %
            strSection % pmtEntries->size ());
    }

    return bSingle;
}

/** Parses a set of strings into IP::Endpoint
      Strings which fail to parse are not included in the output. If a stream is
      provided, human readable diagnostic error messages are written for each
      failed parse.
      @param out An OutputSequence to store the IP::Endpoint list
      @param first The begining of the string input sequence
      @param last The one-past-the-end of the string input sequence
*/
template <class OutputSequence, class InputIterator>
void
parseAddresses (OutputSequence& out, InputIterator first, InputIterator last,
    beast::Journal::Stream stream = beast::Journal::Stream ())
{
    while (first != last)
    {
        auto const str (*first);
        ++first;
        {
            beast::IP::Endpoint const addr (
                beast::IP::Endpoint::from_string (str));
            if (! is_unspecified (addr))
            {
                out.push_back (addr);
                continue;
            }
        }
        {
            beast::IP::Endpoint const addr (
                beast::IP::Endpoint::from_string_altform (str));
            if (! is_unspecified (addr))
            {
                out.push_back (addr);
                continue;
            }
        }
        if (stream) stream <<
            "Config: \"" << str << "\" is not a valid IP address.";
    }
}

//------------------------------------------------------------------------------
//
// Config (DEPRECATED)
//
//------------------------------------------------------------------------------

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

void Config::setup (std::string const& strConf, bool bQuiet)
{
    boost::filesystem::path dataDir;
    boost::system::error_code   ec;
    std::string                 strDbPath, strConfFile;

    //
    // Determine the config and data directories.
    // If the config file is found in the current working directory, use the current working directory as the config directory and
    // that with "db" as the data directory.
    //

    QUIET       = bQuiet;

    strDbPath           = Helpers::getDatabaseDirName ();
    strConfFile         = strConf.empty () ? Helpers::getConfigFileName () : strConf;

    VALIDATORS_BASE     = Helpers::getValidatorsFileName ();

    VALIDATORS_URI      = boost::str (boost::format ("/%s") % VALIDATORS_BASE);

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

            boost::filesystem::create_directories (CONFIG_DIR, ec);

            if (ec)
                throw std::runtime_error (boost::str (boost::format ("Can not create %s") % CONFIG_DIR));
        }
    }

    // Update default values
    load ();
    {
        // load() may have set a new value for the dataDir
        std::string const dbPath (legacy ("database_path"));
        if (!dbPath.empty ())
        {
            dataDir = boost::filesystem::path (dbPath);
        }
    }

    boost::filesystem::create_directories (dataDir, ec);

    if (ec)
        throw std::runtime_error (
            boost::str (boost::format ("Can not create %s") % dataDir));

    legacy ("database_path", boost::filesystem::absolute (dataDir).string ());

    HTTPClient::initializeSSLContext(*this);
}

void Config::load ()
{
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

    if (auto s = getIniFileSection (secConfig, SECTION_VALIDATORS))
        validators  = *s;

    if (auto s = getIniFileSection (secConfig, SECTION_CLUSTER_NODES))
        CLUSTER_NODES = *s;

    if (auto s = getIniFileSection (secConfig, SECTION_IPS))
        IPS = *s;

    if (auto s = getIniFileSection (secConfig, SECTION_IPS_FIXED))
        IPS_FIXED = *s;

    if (auto s = getIniFileSection (secConfig, SECTION_SNTP))
        SNTP_SERVERS = *s;

    if (auto s = getIniFileSection (secConfig, SECTION_RPC_STARTUP))
    {
        RPC_STARTUP = Json::arrayValue;

        for (auto const& strJson : *s)
        {
            Json::Reader    jrReader;
            Json::Value     jvCommand;

            if (!jrReader.parse (strJson, jvCommand))
                throw std::runtime_error (
                    boost::str (boost::format (
                        "Couldn't parse [" SECTION_RPC_STARTUP "] command: %s") % strJson));

            RPC_STARTUP.append (jvCommand);
        }
    }

    {
        std::string dbPath;
        if (getSingleSection (secConfig, "database_path", dbPath, j_))
        {
            boost::filesystem::path p(dbPath);
            legacy("database_path",
                   boost::filesystem::absolute (p).string ());
        }
    }

    (void) getSingleSection (secConfig, SECTION_VALIDATORS_SITE, VALIDATORS_SITE, j_);

    std::string strTemp;
    if (getSingleSection (secConfig, SECTION_PEER_PRIVATE, strTemp, j_))
        PEER_PRIVATE        = beast::lexicalCastThrow <bool> (strTemp);

    if (getSingleSection (secConfig, SECTION_PEERS_MAX, strTemp, j_))
        PEERS_MAX           = beast::lexicalCastThrow <int> (strTemp);

    if (getSingleSection (secConfig, SECTION_NODE_SIZE, strTemp, j_))
    {
        if (strTemp == "tiny")
            NODE_SIZE = 0;
        else if (strTemp == "small")
            NODE_SIZE = 1;
        else if (strTemp == "medium")
            NODE_SIZE = 2;
        else if (strTemp == "large")
            NODE_SIZE = 3;
        else if (strTemp == "huge")
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
        WEBSOCKET_PING_FREQ = beast::lexicalCastThrow <int> (strTemp);

    getSingleSection (secConfig, SECTION_SSL_VERIFY_FILE, SSL_VERIFY_FILE, j_);
    getSingleSection (secConfig, SECTION_SSL_VERIFY_DIR, SSL_VERIFY_DIR, j_);

    if (getSingleSection (secConfig, SECTION_SSL_VERIFY, strTemp, j_))
        SSL_VERIFY          = beast::lexicalCastThrow <bool> (strTemp);

    if (getSingleSection (secConfig, SECTION_VALIDATION_SEED, strTemp, j_))
    {
        VALIDATION_SEED.setSeedGeneric (strTemp);

        if (VALIDATION_SEED.isValid ())
        {
            VALIDATION_PUB = RippleAddress::createNodePublic (VALIDATION_SEED);
            VALIDATION_PRIV = RippleAddress::createNodePrivate (VALIDATION_SEED);
        }
    }

    if (getSingleSection (secConfig, SECTION_NODE_SEED, strTemp, j_))
    {
        NODE_SEED.setSeedGeneric (strTemp);

        if (NODE_SEED.isValid ())
        {
            NODE_PUB = RippleAddress::createNodePublic (NODE_SEED);
            NODE_PRIV = RippleAddress::createNodePrivate (NODE_SEED);
        }
    }

    if (getSingleSection (secConfig, SECTION_NETWORK_QUORUM, strTemp, j_))
        NETWORK_QUORUM      = beast::lexicalCastThrow <std::size_t> (strTemp);

    if (getSingleSection (secConfig, SECTION_VALIDATION_QUORUM, strTemp, j_))
        VALIDATION_QUORUM   = std::max (0, beast::lexicalCastThrow <int> (strTemp));

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
        boost::to_lower (strTemp);

        if (strTemp == "full")
            LEDGER_HISTORY = 1000000000u;
        else if (strTemp == "none")
            LEDGER_HISTORY = 0;
        else
            LEDGER_HISTORY = beast::lexicalCastThrow <std::uint32_t> (strTemp);
    }

    if (getSingleSection (secConfig, SECTION_FETCH_DEPTH, strTemp, j_))
    {
        boost::to_lower (strTemp);

        if (strTemp == "none")
            FETCH_DEPTH = 0;
        else if (strTemp == "full")
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

    if (getSingleSection (secConfig, SECTION_VALIDATORS_FILE, strTemp, j_))
    {
        VALIDATORS_FILE     = strTemp;
    }

    if (getSingleSection (secConfig, SECTION_DEBUG_LOGFILE, strTemp, j_))
        DEBUG_LOGFILE       = strTemp;

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

        { siValidationsSize,    {   256,    256,    512,    1024,       1024    } },
        { siValidationsAge,     {   500,    500,    500,    500,        500     } },

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

beast::File Config::getConfigDir () const
{
    beast::String const s (CONFIG_FILE.native().c_str ());
    if (s.isNotEmpty ())
        return beast::File (s).getParentDirectory ();
    return beast::File::nonexistent ();
}

beast::File Config::getValidatorsFile () const
{
    beast::String const s (VALIDATORS_FILE.native().c_str());
    if (s.isNotEmpty() && getConfigDir() != beast::File::nonexistent())
        return getConfigDir().getChildFile (s);
    return beast::File::nonexistent ();
}

beast::URL Config::getValidatorsURL () const
{
    return beast::parse_URL (VALIDATORS_SITE).second;
}

beast::File Config::getModuleDatabasePath () const
{
    boost::filesystem::path dbPath (legacy ("database_path"));

    beast::String const s (dbPath.native ().c_str ());
    if (s.isNotEmpty ())
        return beast::File (s);
    return beast::File::nonexistent ();
}

} // ripple
