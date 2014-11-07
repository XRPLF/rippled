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
#include <ripple/core/SystemParameters.h>
#include <ripple/protocol/RippleAddress.h>
#include <ripple/unity/json.h>
#include <beast/http/URL.h>
#include <beast/net/IPEndpoint.h>
#include <beast/module/core/files/File.h>
#include <beast/module/core/text/StringPairArray.h>
#include <beast/utility/ci_char_traits.h>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

namespace ripple {

IniFileSections
parseIniFile (std::string const& strInput, const bool bTrim);

bool
getSingleSection (IniFileSections& secSource,
    std::string const& strSection, std::string& strValue);

int
countSectionEntries (IniFileSections& secSource, std::string const& strSection);

IniFileSections::mapped_type*
getIniFileSection (IniFileSections& secSource, std::string const& strSection);

/** Parse a section of lines as a key/value array.
    Each line is in the form <key>=<value>.
    Spaces are considered part of the key and value.
*/
// DEPRECATED
beast::StringPairArray
parseKeyValueSection (IniFileSections& secSource, std::string const& strSection);

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

// VFALCO NOTE This entire derived class is deprecated
//             For new config information use the style implied
//             in the base class. For existing config information
//             try to refactor code to use the new style.
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

    /** Returns the directory in which the current database files are located. */
    beast::File getDatabaseDir () const;

    /** Returns the full path and filename of the debug log file. */
    boost::filesystem::path getDebugLogFile () const;

    // LEGACY FIELDS, REMOVE ASAP
    boost::filesystem::path CONFIG_FILE; // used by UniqueNodeList
private:
    boost::filesystem::path CONFIG_DIR;
    boost::filesystem::path DEBUG_LOGFILE;
public:
    // VFALCO TODO Make this private and fix callers to go through getDatabaseDir()
    boost::filesystem::path DATA_DIR;

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

private:
    /** The folder where new module databases should be located */
    beast::File m_moduleDbPath;

public:
    //--------------------------------------------------------------------------
    /** Returns the location were databases should be located
        The location may be a file, in which case databases should be placed in
        the file, or it may be a directory, in which cases databases should be
        stored in a file named after the module (e.g. "peerfinder.sqlite") that
        is inside that directory.
    */
    beast::File const& getModuleDatabasePath ();

    //--------------------------------------------------------------------------

    /** Parameters for the insight collection module */
    beast::StringPairArray insightSettings;

    /** Parameters for the main NodeStore database.

        This is 1 or more strings of the form <key>=<value>
        The 'type' and 'path' keys are required, see rippled-example.cfg

        @see Database
    */
    beast::StringPairArray nodeDatabase;

    /** Parameters for the ephemeral NodeStore database.

        This is an auxiliary database for the NodeStore, usually placed
        on a separate faster volume. However, the volume data may not persist
        between launches. Use of the ephemeral database is optional.

        The format is the same as that for @ref nodeDatabase

        @see Database
    */
    beast::StringPairArray ephemeralNodeDatabase;

    /** Parameters for importing an old database in to the current node database.
        If this is not empty, then it specifies the key/value parameters for
        another node database from which to import all data into the current
        node database specified by @ref nodeDatabase.
        The format of this string is in the form:
            <key>'='<value>['|'<key>'='value]
        @see parseDelimitedKeyValueString
    */
    bool doImport;
    beast::StringPairArray importNodeDatabase;

    //
    //
    //--------------------------------------------------------------------------
public:
    // Configuration parameters
    bool                        QUIET;

    bool                        ELB_SUPPORT;            // Support Amazon ELB

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
    StartUpType                 START_UP;



    std::string                 START_LEDGER;

    // Database
    std::string                 DATABASE_PATH;

    // Network parameters
    int                         TRANSACTION_FEE_BASE;   // The number of fee units a reference transaction costs

    /** Operate in stand-alone mode.

        In stand alone mode:

        - Peer connections are not attempted or accepted
        - The ledger is not advanced automatically.
        - If no ledger is loaded, the default ledger with the root
          account is created.
    */
    bool                        RUN_STANDALONE;

    // Note: The following parameters do not relate to the UNL or trust at all
    std::size_t                 NETWORK_QUORUM;         // Minimum number of nodes to consider the network present
    int                         VALIDATION_QUORUM;      // Minimum validations to consider ledger authoritative

    // Peer networking parameters
    bool                        PEER_PRIVATE;           // True to ask peers not to relay current IP.
    unsigned int                PEERS_MAX;

    int                         WEBSOCKET_PING_FREQ;

    // RPC parameters
    std::vector<beast::IP::Endpoint>   RPC_ADMIN_ALLOW;
    Json::Value                     RPC_STARTUP;

    // Path searching
    int                         PATH_SEARCH_OLD;
    int                         PATH_SEARCH;
    int                         PATH_SEARCH_FAST;
    int                         PATH_SEARCH_MAX;

    // Validation
    RippleAddress               VALIDATION_SEED;
    RippleAddress               VALIDATION_PUB;
    RippleAddress               VALIDATION_PRIV;

    // Node/Cluster
    std::vector<std::string>    CLUSTER_NODES;
    RippleAddress               NODE_SEED;
    RippleAddress               NODE_PUB;
    RippleAddress               NODE_PRIV;

    // Fee schedule (All below values are in fee units)
    std::uint64_t                      FEE_DEFAULT;            // Default fee.
    std::uint64_t                      FEE_ACCOUNT_RESERVE;    // Amount of units not allowed to send.
    std::uint64_t                      FEE_OWNER_RESERVE;      // Amount of units not allowed to send per owner entry.
    std::uint64_t                      FEE_OFFER;              // Rate per day.
    int                                FEE_CONTRACT_OPERATION; // fee for each contract operation

    // Node storage configuration
    std::uint32_t                      LEDGER_HISTORY;
    std::uint32_t                      FETCH_DEPTH;
    int                         NODE_SIZE;

    // Client behavior
    int                         ACCOUNT_PROBE_MAX;      // How far to scan for accounts.

    bool                        SSL_VERIFY;
    std::string                 SSL_VERIFY_FILE;
    std::string                 SSL_VERIFY_DIR;

    std::string                 SMS_FROM;
    std::string                 SMS_KEY;
    std::string                 SMS_SECRET;
    std::string                 SMS_TO;
    std::string                 SMS_URL;

public:
    Config ();

    int getSize (SizedItemName);
    void setup (std::string const& strConf, bool bQuiet);
    void load ();
};

// VFALCO DEPRECATED
extern Config& getConfig();

} // ripple

#endif
