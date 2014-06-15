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

#include <ripple/unity/json.h>
#include <ripple/module/data/protocol/RippleAddress.h>
#include <beast/module/core/files/File.h>
#include <beast/http/URL.h>
#include <boost/filesystem.hpp>
#include <cstdint>
#include <string>

namespace ripple {

// VFALCO TODO Replace these with beast "unsigned long long" generators
// VFALCO NOTE Apparently these are used elsewhere. Make them constants in the config
//             or in the Application
//
#define SYSTEM_CURRENCY_GIFT        1000ull
#define SYSTEM_CURRENCY_USERS       100000000ull
#define SYSTEM_CURRENCY_PARTS       1000000ull      // 10^SYSTEM_CURRENCY_PRECISION
#define SYSTEM_CURRENCY_START       (SYSTEM_CURRENCY_GIFT*SYSTEM_CURRENCY_USERS*SYSTEM_CURRENCY_PARTS)

const int DOMAIN_BYTES_MAX              = 256;
const int PUBLIC_BYTES_MAX              = 33;       // Maximum bytes for an account public key.

const int SYSTEM_PEER_PORT              = 6561;
const int SYSTEM_WEBSOCKET_PORT         = 6562;
const int SYSTEM_WEBSOCKET_PUBLIC_PORT  = 6563; // XXX Going away.

// Allow anonymous DH.
#define DEFAULT_PEER_SSL_CIPHER_LIST    "ALL:!LOW:!EXP:!MD5:@STRENGTH"

// Normal, recommend 1 hour: 60*60
// Testing, recommend 1 minute: 60
#define DEFAULT_PEER_SCAN_INTERVAL_MIN  (60*60) // Seconds

// Maximum number of peers to try to connect to as client at once.
#define DEFAULT_PEER_START_MAX          5

// Might connect with fewer for testing.
#define DEFAULT_PEER_CONNECT_LOW_WATER  10

#define DEFAULT_PATH_SEARCH_OLD         7
#define DEFAULT_PATH_SEARCH             7
#define DEFAULT_PATH_SEARCH_FAST        2
#define DEFAULT_PATH_SEARCH_MAX         10

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

// VFALCO TODO rename all fields to not look like macros, and be more verbose
// VFALCO TODO document every member
class Config
{
public:
    //--------------------------------------------------------------------------
    //
    // VFALCO NOTE To tame this "Config" beast I am breaking it up into
    //             individual sections related to a specific area of the
    //             program. For example, listening port configuration. Or
    //             node database configuration. Each class has its own
    //             default constructor, and load function for reading in
    //             settings from the parsed config file data.
    //
    //             Clean member area. Please follow this style for modifying
    //             or adding code in the file.

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

    /** The result of performing a load on the parsed config file data.
        This type is convertible to `bool`.
        A value of `true` indicates an error occurred,
        while `false` indicates no error.
    */
    class Error
    {
    public:
        Error () noexcept
            : m_what (beast::String::empty)
            , m_fileName ("")
            , m_lineNumber (0)
        {
        }

        Error (beast::String what, char const* fileName, int lineNumber)  noexcept
            : m_what (what)
            , m_fileName (fileName)
            , m_lineNumber (lineNumber)
        {
        }

        explicit
        operator bool() const noexcept
        {
            return m_what != beast::String::empty;
        }

        beast::String what () const noexcept
        {
            return m_what;
        }

        char const* fileName () const
        {
            return m_fileName;
        }
        
        int lineNumber () const
        {
            return m_lineNumber;
        }

    private:
        beast::String m_what;
        char const* m_fileName;
        int m_lineNumber;
    };

    /** Listening socket settings. */
    struct DoorSettings
    {
        /** Create a default set of door (listening socket) settings. */
        DoorSettings ();

        /** Load settings from the configuration file. */
        //Error load (ParsedConfigFile const& file);
    };

    //--------------------------------------------------------------------------

    // Settings related to the configuration file location and directories

    /** Returns the directory from which the configuration file was loaded. */
    beast::File getConfigDir () const;

    /** Returns the directory in which the current database files are located. */
    beast::File getDatabaseDir () const;

    // LEGACY FIELDS, REMOVE ASAP
    boost::filesystem::path CONFIG_FILE; // used by UniqueNodeList
private:
    boost::filesystem::path CONFIG_DIR;
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

    //--------------------------------------------------------------------------

    // Settings related to RPC

    /** Get the client or server RPC IP address.
        @note The string may not always be in a valid parsable state.
        @return A string representing the address.
    */
    std::string getRpcIP () const { return m_rpcIP; }

    /** Get the client or server RPC port number.
        @note The port number may be invalid (out of range or zero)
        @return The RPC port number.
    */
    int getRpcPort () const { return m_rpcPort; }

    /** Set the client or server RPC IP and optional port.
        @note The string is not syntax checked.
        @param newAddress A string in the format <ip-address>[':'<port-number>]
    */
    void setRpcIpAndOptionalPort (std::string const& newAddress);

    /** Set the client or server RPC IP.
        @note The string is not syntax-checked.
        @param newIP A string representing the IP address to use.
    */
    void setRpcIP (std::string const& newIP) { m_rpcIP = newIP; }

    /** Set the client or server RPC port number.
        @note The port number is not range checked.
        @param newPort The RPC port number to use.
    */
    void setRpcPort (int newPort) { m_rpcPort = newPort; }

    /** Convert the RPC/port combination to a readable string.
    */
    beast::String const getRpcAddress ()
    {
        beast::String s;

        s << m_rpcIP.c_str () << ":" << m_rpcPort;

        return s;
    }

    /** Determine the level of administrative permission to grant.
    */
    enum Role
    {
        GUEST,
        USER,
        ADMIN,
        FORBID
    };
    Role getAdminRole (Json::Value const& params, beast::IP::Endpoint const& remoteIp) const;

    /** Listening port number for peer connections. */
    int peerListeningPort;

    /** PROXY listening port number
        If this is not zero, it indicates an additional port number on
        which we should accept incoming Peer connections that will also
        require a PROXY handshake.

        The PROXY Protocol:
        http://haproxy.1wt.eu/download/1.5/doc/proxy-protocol.txt
    */
    int peerPROXYListeningPort;

    /** List of Validators entries from rippled.cfg */
    std::vector <std::string> validators;

private:
    std::string m_rpcIP;
    int m_rpcPort; // VFALCO TODO This should be a short.

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

    boost::filesystem::path     DEBUG_LOGFILE;
    std::string                 CONSOLE_LOG_OUTPUT;

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
    int                         NETWORK_START_TIME;     // The Unix time we start ledger 0.
    int                         TRANSACTION_FEE_BASE;   // The number of fee units a reference transaction costs
    int                         LEDGER_SECONDS;
    int                         LEDGER_PROPOSAL_DELAY_SECONDS;
    int                         LEDGER_AVALANCHE_SECONDS;
    bool                        LEDGER_CREATOR;         // Should be false unless we are starting a new ledger.

    /** Operate in stand-alone mode.

        In stand alone mode:

        - Peer connections are not attempted or accepted
        - The ledger is not advanced automatically.
        - If no ledger is loaded, the default ledger with the root
          account is created.
    */
    bool                        RUN_STANDALONE;

    // Note: The following parameters do not relate to the UNL or trust at all
    unsigned int                NETWORK_QUORUM;         // Minimum number of nodes to consider the network present
    int                         VALIDATION_QUORUM;      // Minimum validations to consider ledger authoritative

    // Peer networking parameters
    std::string                 PEER_IP;
    int                         NUMBER_CONNECTIONS;
    std::string                 PEER_SSL_CIPHER_LIST;
    int                         PEER_SCAN_INTERVAL_MIN;
    int                         PEER_START_MAX;
    unsigned int                PEER_CONNECT_LOW_WATER;
    bool                        PEER_PRIVATE;           // True to ask peers not to relay current IP.
    unsigned int                PEERS_MAX;

    // Websocket networking parameters
    std::string                 WEBSOCKET_PUBLIC_IP;        // XXX Going away. Merge with the inbound peer connction.
    int                         WEBSOCKET_PUBLIC_PORT;
    int                         WEBSOCKET_PUBLIC_SECURE;

    std::string                 WEBSOCKET_PROXY_IP;        // XXX Going away. Merge with the inbound peer connction.
    int                         WEBSOCKET_PROXY_PORT;
    int                         WEBSOCKET_PROXY_SECURE;

    std::string                 WEBSOCKET_IP;
    int                         WEBSOCKET_PORT;
    int                         WEBSOCKET_SECURE;

    int                         WEBSOCKET_PING_FREQ;

    std::string                 WEBSOCKET_SSL_CERT;
    std::string                 WEBSOCKET_SSL_CHAIN;
    std::string                 WEBSOCKET_SSL_KEY;

    // RPC parameters
    std::vector<beast::IP::Endpoint>   RPC_ADMIN_ALLOW;
    std::string                     RPC_ADMIN_PASSWORD;
    std::string                     RPC_ADMIN_USER;
    std::string                     RPC_PASSWORD;
    std::string                     RPC_USER;
    bool                            RPC_ALLOW_REMOTE;
    Json::Value                     RPC_STARTUP;

    int                         RPC_SECURE;
    std::string                 RPC_SSL_CERT;
    std::string                 RPC_SSL_CHAIN;
    std::string                 RPC_SSL_KEY;

    // Path searching
    int                         PATH_SEARCH_OLD;
    int                         PATH_SEARCH;
    int                         PATH_SEARCH_FAST;
    int                         PATH_SEARCH_MAX;

    // Validation
    RippleAddress               VALIDATION_SEED, VALIDATION_PUB, VALIDATION_PRIV;

    // Node/Cluster
    std::vector<std::string>    CLUSTER_NODES;
    RippleAddress               NODE_SEED, NODE_PUB, NODE_PRIV;

    // Fee schedule (All below values are in fee units)
    std::uint64_t                      FEE_DEFAULT;            // Default fee.
    std::uint64_t                      FEE_ACCOUNT_RESERVE;    // Amount of units not allowed to send.
    std::uint64_t                      FEE_OWNER_RESERVE;      // Amount of units not allowed to send per owner entry.
    std::uint64_t                      FEE_NICKNAME_CREATE;    // Fee to create a nickname.
    std::uint64_t                      FEE_OFFER;              // Rate per day.
    int                         FEE_CONTRACT_OPERATION; // fee for each contract operation

    // Node storage configuration
    std::uint32_t                      LEDGER_HISTORY;
    std::uint32_t                      FETCH_DEPTH;
    int                         NODE_SIZE;

    // Client behavior
    int                         ACCOUNT_PROBE_MAX;      // How far to scan for accounts.

    // Signing signatures.
    std::uint32_t                      SIGN_TRANSACTION;
    std::uint32_t                      SIGN_VALIDATION;
    std::uint32_t                      SIGN_PROPOSAL;

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
    void setup (const std::string& strConf, bool bQuiet);
    void load ();
};

extern Config& getConfig ();

} // ripple

#endif
