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

#include <ripple/core/Config.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/basics/Log.h>
#include <ripple/data/protocol/RippleSystem.h>
#include <ripple/net/HTTPClient.h>
#include <beast/http/ParsedURL.h>
#include <beast/module/core/text/LexicalCast.h>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/regex.hpp>
#include <fstream>
#include <iostream>
    
namespace ripple {

//
// TODO: Check permissions on config file before using it.
//

// Fees are in XRP.
#define DEFAULT_FEE_DEFAULT             10
#define DEFAULT_FEE_ACCOUNT_RESERVE     200*SYSTEM_CURRENCY_PARTS
#define DEFAULT_FEE_OWNER_RESERVE       50*SYSTEM_CURRENCY_PARTS
#define DEFAULT_FEE_OFFER               DEFAULT_FEE_DEFAULT
#define DEFAULT_FEE_OPERATION           1

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
    BOOST_FOREACH (std::string & strValue, vLines)
    {
        if (strValue.empty () || strValue[0] == '#')
        {
            // Blank line or comment, do nothing.
        }
        else if (strValue[0] == '[' && strValue[strValue.length () - 1] == ']')
        {
            // New Section.

            strSection              = strValue.substr (1, strValue.length () - 2);
            secResult[strSection]   = IniFileSections::mapped_type ();
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
    std::string const& strSection, std::string& strValue)
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
        WriteLog (lsWARNING, parseIniFile) << boost::str (boost::format ("Section [%s]: requires 1 line not %d lines.")
                                              % strSection
                                              % pmtEntries->size ());
    }

    return bSingle;
}

beast::StringPairArray
parseKeyValueSection (IniFileSections& secSource,
    beast::String const& strSection)
{
    beast::StringPairArray result;

    // yuck.
    std::string const stdStrSection (strSection.toStdString ());

    typedef IniFileSections::mapped_type Entries;

    Entries* const entries = getIniFileSection (secSource, stdStrSection);

    if (entries != nullptr)
    {
        for (Entries::const_iterator iter = entries->begin ();
            iter != entries->end (); ++iter)
        {
            beast::String const line (iter->c_str ());

            int const equalPos = line.indexOfChar ('=');

            if (equalPos != -1)
            {
                beast::String const key = line.substring (0, equalPos);
                beast::String const value = line.substring (equalPos + 1,
                    line.length ());

                result.set (key, value);
            }
        }
    }

    return result;
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
// Section
//
//------------------------------------------------------------------------------

void
Section::append (std::vector <std::string> const& lines)
{
    // <key> '=' <value>
    static boost::regex const re1 (
        "^"                         // start of line
        "(?:\\s*)"                  // whitespace (optonal)
        "([a-zA-Z][_a-zA-Z0-9]*)"   // <key>
        "(?:\\s*)"                  // whitespace (optional)
        "(?:=)"                     // '='
        "(?:\\s*)"                  // whitespace (optional)
        "(.*\\S+)"                  // <value>
        "(?:\\s*)"                  // whitespace (optional)
        , boost::regex_constants::optimize
    );

    lines_.reserve (lines_.size() + lines.size());
    for (auto const& line : lines)
    {
        boost::smatch match;
        lines_.push_back (line);
        if (boost::regex_match (line, match, re1))
        {
            auto const result = map_.emplace (
                std::make_pair (match[1], match[2]));
#if 0
            if (! result.second)
            {
                // If we decide on how to merge values we can do it here.
            }
            beast::debug_ostream log;
            //log << "\"" << match[1] << "\" = \"" << match[2] << "\"";
#endif
        }
    }
}

bool
Section::exists (std::string const& name) const
{
    return map_.find (name) != map_.end();
}

std::pair <std::string, bool>
Section::find (std::string const& name) const
{
    auto const iter = map_.find (name);
    if (iter == map_.end())
        return {{}, false};
    return {iter->second, true};
}

//------------------------------------------------------------------------------
//
// BasicConfig
//
//------------------------------------------------------------------------------

bool
BasicConfig::exists (std::string const& name) const
{
    return map_.find (name) != map_.end();
}

Section const&
BasicConfig::section (std::string const& name) const
{
    static Section none;
    auto const iter = map_.find (name);
    if (iter == map_.end())
        return none;
    return iter->second;
}

void
BasicConfig::build (IniFileSections const& ifs)
{
    for (auto const& entry : ifs)
    {
        auto const result = map_.insert (std::make_pair (
            entry.first, Section{}));
        result.first->second.append (entry.second);
    }
}

//------------------------------------------------------------------------------
//
// Config (DEPRECATED)
//
//------------------------------------------------------------------------------

Config::Config ()
    : m_rpcPort (5001)
{
    //--------------------------------------------------------------------------
    //
    // VFALCO NOTE Clean member area
    //

    peerListeningPort = SYSTEM_PEER_PORT;

    peerPROXYListeningPort = 0;

    //
    //
    //
    //--------------------------------------------------------------------------

    //
    // Defaults
    //

    RPC_SECURE              = 0;
    WEBSOCKET_PORT          = 6562;
    WEBSOCKET_PUBLIC_PORT   = 6563;
    WEBSOCKET_PUBLIC_SECURE = 1;
    WEBSOCKET_PROXY_PORT    = 0;
    WEBSOCKET_PROXY_SECURE  = 1;
    WEBSOCKET_SECURE        = 0;
    WEBSOCKET_PING_FREQ     = (5 * 60);

    RPC_ALLOW_REMOTE        = false;
    RPC_ADMIN_ALLOW.push_back (beast::IP::Endpoint::from_string("127.0.0.1"));

    // By default, allow anonymous DH.
    PEER_SSL_CIPHER_LIST    = "ALL:!LOW:!EXP:!MD5:@STRENGTH";

    PEER_PRIVATE            = false;
    PEERS_MAX               = 0;    // indicates "use default"

    TRANSACTION_FEE_BASE    = DEFAULT_FEE_DEFAULT;

    NETWORK_QUORUM          = 0;    // Don't need to see other nodes
    VALIDATION_QUORUM       = 1;    // Only need one node to vouch

    FEE_ACCOUNT_RESERVE     = DEFAULT_FEE_ACCOUNT_RESERVE;
    FEE_OWNER_RESERVE       = DEFAULT_FEE_OWNER_RESERVE;
    FEE_OFFER               = DEFAULT_FEE_OFFER;
    FEE_DEFAULT             = DEFAULT_FEE_DEFAULT;
    FEE_CONTRACT_OPERATION  = DEFAULT_FEE_OPERATION;

    LEDGER_HISTORY          = 256;
    FETCH_DEPTH             = 1000000000;

    // An explanation of these magical values would be nice.
    PATH_SEARCH_OLD         = 7;
    PATH_SEARCH             = 7;
    PATH_SEARCH_FAST        = 2;
    PATH_SEARCH_MAX         = 10;

    ACCOUNT_PROBE_MAX       = 10;

    VALIDATORS_SITE         = "";

    SSL_VERIFY              = true;

    ELB_SUPPORT             = false;
    RUN_STANDALONE          = false;
    doImport                = false;
    START_UP                = NORMAL;
}

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
    boost::system::error_code   ec;
    std::string                 strDbPath, strConfFile;

    //
    // Determine the config and data directories.
    // If the config file is found in the current working directory, use the current working directory as the config directory and
    // that with "db" as the data directory.
    //

    QUIET       = bQuiet;
    NODE_SIZE   = 0;

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
        DATA_DIR                = CONFIG_DIR / strDbPath;
    }
    else
    {
        CONFIG_DIR              = boost::filesystem::current_path ();
        CONFIG_FILE             = CONFIG_DIR / strConfFile;
        DATA_DIR                = CONFIG_DIR / strDbPath;

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

            CONFIG_DIR  = strXdgConfigHome + "/" SYSTEM_NAME;
            CONFIG_FILE = CONFIG_DIR / strConfFile;
            DATA_DIR    = strXdgDataHome + "/" SYSTEM_NAME;

            boost::filesystem::create_directories (CONFIG_DIR, ec);

            if (ec)
                throw std::runtime_error (boost::str (boost::format ("Can not create %s") % CONFIG_DIR));
        }
    }

    HTTPClient::initializeSSLContext();

    // Update default values
    load ();

    boost::filesystem::create_directories (DATA_DIR, ec);

    if (ec)
        throw std::runtime_error (boost::str (boost::format ("Can not create %s") % DATA_DIR));

    // Create the new unified database
    m_moduleDbPath = getDatabaseDir();

    // This code is temporarily disabled, and modules will fall back to using
    // per-module databases (e.g. "peerfinder.sqlite") under the module db path
    //
    //if (m_moduleDbPath.isDirectory ())
    //    m_moduleDbPath = m_moduleDbPath.getChildFile("rippled.sqlite");
}

void Config::load ()
{
    if (!QUIET)
        std::cerr << "Loading: " << CONFIG_FILE << std::endl;

    std::ifstream   ifsConfig (CONFIG_FILE.c_str (), std::ios::in);

    if (!ifsConfig)
    {
        std::cerr << "Failed to open '" << CONFIG_FILE << "'." << std::endl;
    }
    else
    {
        std::string file_contents;
        file_contents.assign ((std::istreambuf_iterator<char> (ifsConfig)),
                              std::istreambuf_iterator<char> ());

        if (ifsConfig.bad ())
        {
           std::cerr << "Failed to read '" << CONFIG_FILE << "'." << std::endl;
        }
        else
        {
            IniFileSections secConfig = parseIniFile (file_contents, true);
            std::string strTemp;

            build (secConfig);

            // XXX Leak
            IniFileSections::mapped_type* smtTmp;

            smtTmp = getIniFileSection (secConfig, SECTION_VALIDATORS);

            if (smtTmp)
            {
                validators  = *smtTmp;
            }

            smtTmp = getIniFileSection (secConfig, SECTION_CLUSTER_NODES);

            if (smtTmp)
            {
                CLUSTER_NODES = *smtTmp;
            }

            smtTmp  = getIniFileSection (secConfig, SECTION_IPS);

            if (smtTmp)
            {
                IPS = *smtTmp;
            }

            smtTmp  = getIniFileSection (secConfig, SECTION_IPS_FIXED);

            if (smtTmp)
            {
                IPS_FIXED = *smtTmp;
            }

            smtTmp = getIniFileSection (secConfig, SECTION_SNTP);

            if (smtTmp)
            {
                SNTP_SERVERS = *smtTmp;
            }

            smtTmp  = getIniFileSection (secConfig, SECTION_RPC_STARTUP);

            if (smtTmp)
            {
                RPC_STARTUP = Json::arrayValue;

                BOOST_FOREACH (std::string const& strJson, *smtTmp)
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

            if (getSingleSection (secConfig, SECTION_DATABASE_PATH, DATABASE_PATH))
                DATA_DIR    = DATABASE_PATH;

            (void) getSingleSection (secConfig, SECTION_VALIDATORS_SITE, VALIDATORS_SITE);

            (void) getSingleSection (secConfig, SECTION_PEER_IP, PEER_IP);

            if (getSingleSection (secConfig, SECTION_PEER_PRIVATE, strTemp))
                PEER_PRIVATE        = beast::lexicalCastThrow <bool> (strTemp);

            if (getSingleSection (secConfig, SECTION_PEERS_MAX, strTemp))
                PEERS_MAX           = beast::lexicalCastThrow <int> (strTemp);

            smtTmp = getIniFileSection (secConfig, SECTION_RPC_ADMIN_ALLOW);

            if (smtTmp)
            {
                std::vector<beast::IP::Endpoint> parsedAddresses;
                //parseAddresses<std::vector<beast::IP::Endpoint>, std::vector<std::string>::const_iterator>
                //    (parsedAddresses, (*smtTmp).cbegin(), (*smtTmp).cend());
                parseAddresses (parsedAddresses, (*smtTmp).cbegin(), (*smtTmp).cend());
                RPC_ADMIN_ALLOW.insert (RPC_ADMIN_ALLOW.end(),
                        parsedAddresses.cbegin (), parsedAddresses.cend ());
            }

            (void) getSingleSection (secConfig, SECTION_RPC_ADMIN_PASSWORD, RPC_ADMIN_PASSWORD);
            (void) getSingleSection (secConfig, SECTION_RPC_ADMIN_USER, RPC_ADMIN_USER);
            (void) getSingleSection (secConfig, SECTION_RPC_IP, m_rpcIP);
            (void) getSingleSection (secConfig, SECTION_RPC_PASSWORD, RPC_PASSWORD);
            (void) getSingleSection (secConfig, SECTION_RPC_USER, RPC_USER);

            insightSettings = parseKeyValueSection (secConfig, SECTION_INSIGHT);

            //---------------------------------------
            //
            // VFALCO BEGIN CLEAN
            //
            nodeDatabase = parseKeyValueSection (
                secConfig, ConfigSection::nodeDatabase ());

            ephemeralNodeDatabase = parseKeyValueSection (
                secConfig, ConfigSection::tempNodeDatabase ());

            importNodeDatabase = parseKeyValueSection (
                secConfig, ConfigSection::importNodeDatabase ());

            if (getSingleSection (secConfig, SECTION_PEER_PORT, strTemp))
                peerListeningPort = beast::lexicalCastThrow <int> (strTemp);

            if (getSingleSection (secConfig, SECTION_PEER_PROXY_PORT, strTemp))
            {
                peerPROXYListeningPort = beast::lexicalCastThrow <int> (strTemp);

                if (peerPROXYListeningPort != 0 && peerPROXYListeningPort == peerListeningPort)
                    throw std::runtime_error ("Peer and proxy listening ports can't be the same.");
            }
            else
            {
                peerPROXYListeningPort = 0;
            }

            //
            // VFALCO END CLEAN
            //
            //---------------------------------------

            if (getSingleSection (secConfig, SECTION_RPC_PORT, strTemp))
                m_rpcPort = beast::lexicalCastThrow <int> (strTemp);

            if (getSingleSection (secConfig, SECTION_RPC_ALLOW_REMOTE, strTemp))
                RPC_ALLOW_REMOTE    = beast::lexicalCastThrow <bool> (strTemp);

            if (getSingleSection (secConfig, SECTION_NODE_SIZE, strTemp))
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

            if (getSingleSection (secConfig, SECTION_ELB_SUPPORT, strTemp))
                ELB_SUPPORT         = beast::lexicalCastThrow <bool> (strTemp);

            (void) getSingleSection (secConfig, SECTION_WEBSOCKET_IP, WEBSOCKET_IP);

            if (getSingleSection (secConfig, SECTION_WEBSOCKET_PORT, strTemp))
                WEBSOCKET_PORT      = beast::lexicalCastThrow <int> (strTemp);

            (void) getSingleSection (secConfig, SECTION_WEBSOCKET_PUBLIC_IP, WEBSOCKET_PUBLIC_IP);

            if (getSingleSection (secConfig, SECTION_WEBSOCKET_PUBLIC_PORT, strTemp))
                WEBSOCKET_PUBLIC_PORT   = beast::lexicalCastThrow <int> (strTemp);

            (void) getSingleSection (secConfig, SECTION_WEBSOCKET_PROXY_IP, WEBSOCKET_PROXY_IP);

            if (getSingleSection (secConfig, SECTION_WEBSOCKET_PROXY_PORT, strTemp))
                WEBSOCKET_PROXY_PORT   = beast::lexicalCastThrow <int> (strTemp);

            if (getSingleSection (secConfig, SECTION_WEBSOCKET_SECURE, strTemp))
                WEBSOCKET_SECURE    = beast::lexicalCastThrow <int> (strTemp);

            if (getSingleSection (secConfig, SECTION_WEBSOCKET_PUBLIC_SECURE, strTemp))
                WEBSOCKET_PUBLIC_SECURE = beast::lexicalCastThrow <int> (strTemp);

            if (getSingleSection (secConfig, SECTION_WEBSOCKET_PROXY_SECURE, strTemp))
                WEBSOCKET_PROXY_SECURE = beast::lexicalCastThrow <int> (strTemp);

            if (getSingleSection (secConfig, SECTION_WEBSOCKET_PING_FREQ, strTemp))
                WEBSOCKET_PING_FREQ = beast::lexicalCastThrow <int> (strTemp);

            getSingleSection (secConfig, SECTION_WEBSOCKET_SSL_CERT, WEBSOCKET_SSL_CERT);
            getSingleSection (secConfig, SECTION_WEBSOCKET_SSL_CHAIN, WEBSOCKET_SSL_CHAIN);
            getSingleSection (secConfig, SECTION_WEBSOCKET_SSL_KEY, WEBSOCKET_SSL_KEY);

            if (getSingleSection (secConfig, SECTION_RPC_SECURE, strTemp))
                RPC_SECURE  = beast::lexicalCastThrow <int> (strTemp);

            getSingleSection (secConfig, SECTION_RPC_SSL_CERT, RPC_SSL_CERT);
            getSingleSection (secConfig, SECTION_RPC_SSL_CHAIN, RPC_SSL_CHAIN);
            getSingleSection (secConfig, SECTION_RPC_SSL_KEY, RPC_SSL_KEY);


            getSingleSection (secConfig, SECTION_SSL_VERIFY_FILE, SSL_VERIFY_FILE);
            getSingleSection (secConfig, SECTION_SSL_VERIFY_DIR, SSL_VERIFY_DIR);

            if (getSingleSection (secConfig, SECTION_SSL_VERIFY, strTemp))
                SSL_VERIFY          = beast::lexicalCastThrow <bool> (strTemp);

            if (getSingleSection (secConfig, SECTION_VALIDATION_SEED, strTemp))
            {
                VALIDATION_SEED.setSeedGeneric (strTemp);

                if (VALIDATION_SEED.isValid ())
                {
                    VALIDATION_PUB = RippleAddress::createNodePublic (VALIDATION_SEED);
                    VALIDATION_PRIV = RippleAddress::createNodePrivate (VALIDATION_SEED);
                }
            }

            if (getSingleSection (secConfig, SECTION_NODE_SEED, strTemp))
            {
                NODE_SEED.setSeedGeneric (strTemp);

                if (NODE_SEED.isValid ())
                {
                    NODE_PUB = RippleAddress::createNodePublic (NODE_SEED);
                    NODE_PRIV = RippleAddress::createNodePrivate (NODE_SEED);
                }
            }

            (void) getSingleSection (secConfig, SECTION_PEER_SSL_CIPHER_LIST, PEER_SSL_CIPHER_LIST);

            if (getSingleSection (secConfig, SECTION_NETWORK_QUORUM, strTemp))
                NETWORK_QUORUM      = beast::lexicalCastThrow <std::size_t> (strTemp);

            if (getSingleSection (secConfig, SECTION_VALIDATION_QUORUM, strTemp))
                VALIDATION_QUORUM   = std::max (0, beast::lexicalCastThrow <int> (strTemp));

            if (getSingleSection (secConfig, SECTION_FEE_ACCOUNT_RESERVE, strTemp))
                FEE_ACCOUNT_RESERVE = beast::lexicalCastThrow <std::uint64_t> (strTemp);

            if (getSingleSection (secConfig, SECTION_FEE_OWNER_RESERVE, strTemp))
                FEE_OWNER_RESERVE   = beast::lexicalCastThrow <std::uint64_t> (strTemp);

            if (getSingleSection (secConfig, SECTION_FEE_OFFER, strTemp))
                FEE_OFFER           = beast::lexicalCastThrow <int> (strTemp);

            if (getSingleSection (secConfig, SECTION_FEE_DEFAULT, strTemp))
                FEE_DEFAULT         = beast::lexicalCastThrow <int> (strTemp);

            if (getSingleSection (secConfig, SECTION_FEE_OPERATION, strTemp))
                FEE_CONTRACT_OPERATION  = beast::lexicalCastThrow <int> (strTemp);

            if (getSingleSection (secConfig, SECTION_LEDGER_HISTORY, strTemp))
            {
                boost::to_lower (strTemp);

                if (strTemp == "full")
                    LEDGER_HISTORY = 1000000000u;
                else if (strTemp == "none")
                    LEDGER_HISTORY = 0;
                else
                    LEDGER_HISTORY = beast::lexicalCastThrow <std::uint32_t> (strTemp);
            }
            if (getSingleSection (secConfig, SECTION_FETCH_DEPTH, strTemp))
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

            if (getSingleSection (secConfig, SECTION_PATH_SEARCH_OLD, strTemp))
                PATH_SEARCH_OLD     = beast::lexicalCastThrow <int> (strTemp);
            if (getSingleSection (secConfig, SECTION_PATH_SEARCH, strTemp))
                PATH_SEARCH         = beast::lexicalCastThrow <int> (strTemp);
            if (getSingleSection (secConfig, SECTION_PATH_SEARCH_FAST, strTemp))
                PATH_SEARCH_FAST    = beast::lexicalCastThrow <int> (strTemp);
            if (getSingleSection (secConfig, SECTION_PATH_SEARCH_MAX, strTemp))
                PATH_SEARCH_MAX     = beast::lexicalCastThrow <int> (strTemp);

            if (getSingleSection (secConfig, SECTION_ACCOUNT_PROBE_MAX, strTemp))
                ACCOUNT_PROBE_MAX   = beast::lexicalCastThrow <int> (strTemp);

            (void) getSingleSection (secConfig, SECTION_SMS_FROM, SMS_FROM);
            (void) getSingleSection (secConfig, SECTION_SMS_KEY, SMS_KEY);
            (void) getSingleSection (secConfig, SECTION_SMS_SECRET, SMS_SECRET);
            (void) getSingleSection (secConfig, SECTION_SMS_TO, SMS_TO);
            (void) getSingleSection (secConfig, SECTION_SMS_URL, SMS_URL);

            if (getSingleSection (secConfig, SECTION_VALIDATORS_FILE, strTemp))
            {
                VALIDATORS_FILE     = strTemp;
            }

            if (getSingleSection (secConfig, SECTION_DEBUG_LOGFILE, strTemp))
                DEBUG_LOGFILE       = strTemp;
        }
    }
}

int Config::getSize (SizedItemName item)
{
    SizedItem sizeTable[] =   //    tiny    small   medium  large       huge
    {

        { siSweepInterval,      {   10,     30,     60,     90,         120     } },

        { siLedgerFetch,        {   2,      2,      3,      3,          3       } },

        { siValidationsSize,    {   256,    256,    512,    1024,       1024    } },
        { siValidationsAge,     {   500,    500,    500,    500,        500     } },

        { siNodeCacheSize,      {   16384,  32768,  131072, 262144,     0       } },
        { siNodeCacheAge,       {   60,     90,     120,    900,        0       } },

        { siTreeCacheSize,      {   128000, 256000, 512000, 768000,     0       } },
        { siTreeCacheAge,       {   30,     60,     90,     120,        900     } },

        { siSLECacheSize,       {   4096,   8192,   16384,  65536,      0       } },
        { siSLECacheAge,        {   30,     60,     90,     120,        300     } },

        { siLedgerSize,         {   32,     128,    256,    384,        0       } },
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
            log_file, getConfig ().CONFIG_DIR);
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

//------------------------------------------------------------------------------
//
// VFALCO NOTE Clean members area
//

Config& getConfig ()
{
    static Config config;
    return config;
}

//------------------------------------------------------------------------------

beast::File Config::getConfigDir () const
{
    beast::String const s (CONFIG_FILE.native().c_str ());
    if (s.isNotEmpty ())
        return beast::File (s).getParentDirectory ();
    return beast::File::nonexistent ();
}

beast::File Config::getDatabaseDir () const
{
    beast::String const s (DATA_DIR.native().c_str());
    if (s.isNotEmpty ())
        return beast::File (s);
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
    //String s = "https://" + VALIDATORS_SITE + VALIDATORS_URI;
    beast::String s = VALIDATORS_SITE;
    return beast::ParsedURL (s).url ();
}

//------------------------------------------------------------------------------

void Config::setRpcIpAndOptionalPort (std::string const& newAddress)
{
    beast::String const s (newAddress.c_str ());

    int const colonPosition = s.lastIndexOfChar (':');

    if (colonPosition != -1)
    {
        beast::String const ipPart = s.substring (0, colonPosition);
        beast::String const portPart = s.substring (colonPosition + 1, s.length ());

        setRpcIP (ipPart.toRawUTF8 ());
        setRpcPort (portPart.getIntValue ());
    }
    else
    {
        setRpcIP (newAddress);
    }
}

//------------------------------------------------------------------------------

Config::Role Config::getAdminRole (Json::Value const& params, beast::IP::Endpoint const& remoteIp) const
{
    Config::Role role (Config::FORBID);

    bool const bPasswordSupplied =
        params.isMember ("admin_user") ||
        params.isMember ("admin_password");

    bool const bPasswordRequired =
        ! this->RPC_ADMIN_USER.empty () ||
        ! this->RPC_ADMIN_PASSWORD.empty ();

    bool bPasswordWrong;

    if (bPasswordSupplied)
    {
        if (bPasswordRequired)
        {
            // Required, and supplied, check match
            bPasswordWrong =
                (this->RPC_ADMIN_USER !=
                    (params.isMember ("admin_user") ? params["admin_user"].asString () : ""))
                ||
                (this->RPC_ADMIN_PASSWORD !=
                    (params.isMember ("admin_user") ? params["admin_password"].asString () : ""));
        }
        else
        {
            // Not required, but supplied
            bPasswordWrong = false;
        }
    }
    else
    {
        // Required but not supplied,
        bPasswordWrong = bPasswordRequired;
    }

    // Meets IP restriction for admin.
    beast::IP::Endpoint const remote_addr (remoteIp.at_port (0));
    bool bAdminIP = false;

    for (auto const& allow_addr : RPC_ADMIN_ALLOW)
    {
        if (allow_addr == remote_addr)
        {
            bAdminIP = true;
            break;
        }
    }

    if (bPasswordWrong                          // Wrong
            || (bPasswordSupplied && !bAdminIP))    // Supplied and doesn't meet IP filter.
    {
        role   = Config::FORBID;
    }
    // If supplied, password is correct.
    else
    {
        // Allow admin, if from admin IP and no password is required or it was supplied and correct.
        role = bAdminIP && (!bPasswordRequired || bPasswordSupplied) ? Config::ADMIN : Config::GUEST;
    }

    return role;
}

beast::File const& Config::getModuleDatabasePath ()
{
    return m_moduleDbPath;
}

} // ripple
