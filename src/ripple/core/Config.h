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

#ifndef RIPPLE_CORE_CONFIG_H_INCLUDED
#define RIPPLE_CORE_CONFIG_H_INCLUDED

#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/base_uint.h>
#include <ripple/protocol/SystemParameters.h> // VFALCO Breaks levelization
#include <ripple/protocol/RippleAddress.h> // VFALCO Breaks levelization
#include <ripple/json/json_value.h>
#include <beast/http/URL.h>
#include <beast/net/IPEndpoint.h>
#include <beast/module/core/files/File.h>
#include <beast/utility/ci_char_traits.h>
#include <beast/utility/Journal.h>
#include <boost/asio/ip/tcp.hpp> // VFALCO FIX: This include should not be here
#include <boost/filesystem.hpp> // VFALCO FIX: This include should not be here
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>
#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace ripple {

IniFileSections
parseIniFile (std::string const& strInput, const bool bTrim);

bool
getSingleSection (IniFileSections& secSource,
    std::string const& strSection, std::string& strValue, beast::Journal j);

int
countSectionEntries (IniFileSections& secSource, std::string const& strSection);

IniFileSections::mapped_type*
getIniFileSection (IniFileSections& secSource, std::string const& strSection);

class Rules;

//------------------------------------------------------------------------------

enum SizedItemName
{
    siSweepInterval,
    siValidationsSize,
    siValidationsAge,
    siNodeCacheSize,
    siNodeCacheAge,
    siTreeCacheSize,
    siTreeCacheAge,
    siSLECacheSize,
    siSLECacheAge,
    siLedgerSize,
    siLedgerAge,
    siLedgerFetch,
    siHashNodeDBCache,
    siTxnDBCache,
    siLgrDBCache,
};

struct SizedItem
{
    SizedItemName   item;
    int             sizes[5];
};

//  This entire derived class is deprecated.
//  For new config information use the style implied
//  in the base class. For existing config information
//  try to refactor code to use the new style.
//
class Config : public BasicConfig
{
public:
    struct Helpers
    {
        // This replaces CONFIG_FILE_NAME
        static char const* getConfigFileName ()
        {
            return "rippled.cfg";
        }

        static char const* getDatabaseDirName ()
        {
            return "db";
        }

        static char const* getValidatorsFileName ()
        {
            return "validators.txt";
        }
    };

    //--------------------------------------------------------------------------

    // Settings related to the configuration file location and directories

    /** Returns the directory from which the configuration file was loaded. */
    beast::File getConfigDir () const;

    /** Returns the full path and filename of the debug log file. */
    boost::filesystem::path getDebugLogFile () const;

    /** Returns the full path and filename of the entropy seed file. */
    boost::filesystem::path getEntropyFile () const;

    // DEPRECATED
    boost::filesystem::path CONFIG_FILE; // used by UniqueNodeList
private:
    boost::filesystem::path CONFIG_DIR;
    boost::filesystem::path DEBUG_LOGFILE;

    void load ();
    beast::Journal j_;
public:

    //--------------------------------------------------------------------------

    // Settings related to validators

    /** Return the path to the separate, optional validators file. */
    beast::File getValidatorsFile () const;

    /** Returns the optional URL to a trusted network source of validators. */
    beast::URL getValidatorsURL () const;

    // DEPRECATED
    boost::filesystem::path     VALIDATORS_FILE;        // As specifed in rippled.cfg.

    /** List of Validators entries from rippled.cfg */
    std::vector <std::string> validators;

public:
    //--------------------------------------------------------------------------
    /** Returns the location were databases should be located
        The location may be a file, in which case databases should be placed in
        the file, or it may be a directory, in which cases databases should be
        stored in a file named after the module (e.g. "peerfinder.sqlite") that
        is inside that directory.
    */
    beast::File getModuleDatabasePath () const;

    bool doImport = false;

    bool                        QUIET = false;

    bool                        ELB_SUPPORT = false;

    std::string                 VALIDATORS_SITE;        // Where to find validators.txt on the Internet.
    std::string                 VALIDATORS_URI;         // URI of validators.txt.
    std::string                 VALIDATORS_BASE;        // Name
    std::vector<std::string>    IPS;                    // Peer IPs from rippled.cfg.
    std::vector<std::string>    IPS_FIXED;              // Fixed Peer IPs from rippled.cfg.
    std::vector<std::string>    SNTP_SERVERS;           // SNTP servers from rippled.cfg.

    enum StartUpType
    {
        FRESH,
        NORMAL,
        LOAD,
        LOAD_FILE,
        REPLAY,
        NETWORK
    };
    StartUpType                 START_UP = NORMAL;

    bool                        START_VALID = false;

    std::string                 START_LEDGER;

    // Network parameters
    int                         TRANSACTION_FEE_BASE = 10;   // The number of fee units a reference transaction costs

    /** Operate in stand-alone mode.

        In stand alone mode:

        - Peer connections are not attempted or accepted
        - The ledger is not advanced automatically.
        - If no ledger is loaded, the default ledger with the root
          account is created.
    */
    bool                        RUN_STANDALONE = false;

    // Note: The following parameters do not relate to the UNL or trust at all
    std::size_t                 NETWORK_QUORUM = 0;         // Minimum number of nodes to consider the network present
    int                         VALIDATION_QUORUM = 1;      // Minimum validations to consider ledger authoritative
    bool                        LOCK_QUORUM = false;        // Do not raise the quorum

    // Peer networking parameters
    bool                        PEER_PRIVATE = false;           // True to ask peers not to relay current IP.
    unsigned int                PEERS_MAX = 0;

    int                         WEBSOCKET_PING_FREQ = 5 * 60;

    // RPC parameters
    Json::Value                     RPC_STARTUP;

    // Path searching
    int                         PATH_SEARCH_OLD = 7;
    int                         PATH_SEARCH = 7;
    int                         PATH_SEARCH_FAST = 2;
    int                         PATH_SEARCH_MAX = 10;

    // Validation
    RippleAddress               VALIDATION_SEED;
    RippleAddress               VALIDATION_PUB;
    RippleAddress               VALIDATION_PRIV;

    // Node/Cluster
    std::vector<std::string>    CLUSTER_NODES;
    RippleAddress               NODE_SEED;
    RippleAddress               NODE_PUB;
    RippleAddress               NODE_PRIV;

    std::uint64_t                      FEE_DEFAULT = 10;
    std::uint64_t                      FEE_ACCOUNT_RESERVE = 200*SYSTEM_CURRENCY_PARTS;
    std::uint64_t                      FEE_OWNER_RESERVE = 50*SYSTEM_CURRENCY_PARTS;
    std::uint64_t                      FEE_OFFER = 10;

    // Node storage configuration
    std::uint32_t                      LEDGER_HISTORY = 256;
    std::uint32_t                      FETCH_DEPTH = 1000000000;
    int                         NODE_SIZE = 0;

    bool                        SSL_VERIFY = true;
    std::string                 SSL_VERIFY_FILE;
    std::string                 SSL_VERIFY_DIR;

    // These override the command line client settings
    boost::optional<boost::asio::ip::address_v4> rpc_ip;
    boost::optional<std::uint16_t> rpc_port;

    std::unordered_set<uint256, beast::uhash<>> features;

public:
    Config() = default;

    int getSize (SizedItemName) const;
    void setup (std::string const& strConf, bool bQuiet);

    /**
     *  Load the conig from the contents of the sting.
     *
     *  @param fileContents String representing the config contents.
     */
    void loadFromString (std::string const& fileContents);
};

} // ripple

#endif
