//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CONFIG_H
#define RIPPLE_CONFIG_H

// VFALCO NOTE Set this to 1 to enable code which is unnecessary
#define ENABLE_INSECURE             0

#define CONFIG_FILE_NAME            SYSTEM_NAME "d.cfg" // rippled.cfg

#define DEFAULT_VALIDATORS_SITE     ""
#define VALIDATORS_FILE_NAME        "validators.txt"

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
#define DEFAULT_PEER_CONNECT_LOW_WATER  4

// Grows exponentially worse.
#define DEFAULT_PATH_SEARCH_SIZE        4

enum SizedItemName
{
    siSweepInterval,
    siValidationsSize,
    siValidationsAge,
    siNodeCacheSize,
    siNodeCacheAge,
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
    // Configuration parameters
    bool                        QUIET;
    bool                        TESTNET;

    boost::filesystem::path     CONFIG_FILE;
    boost::filesystem::path     CONFIG_DIR;
    boost::filesystem::path     DATA_DIR;
    boost::filesystem::path     DEBUG_LOGFILE;
    boost::filesystem::path     VALIDATORS_FILE;        // As specifed in rippled.cfg.
    std::string                 NODE_DB;                // Database to use for nodes
    std::string                 LDB_EPHEMERAL;          // Database for temporary storage
    bool                        LDB_IMPORT;             // Import into LevelDB
    bool                        ELB_SUPPORT;            // Support Amazon ELB

    std::string                 VALIDATORS_SITE;        // Where to find validators.txt on the Internet.
    std::string                 VALIDATORS_URI;         // URI of validators.txt.
    std::string                 VALIDATORS_BASE;        // Name with testnet-, if needed.
    std::vector<std::string>    VALIDATORS;             // Validators from rippled.cfg.
    std::vector<std::string>    IPS;                    // Peer IPs from rippled.cfg.
    std::vector<std::string>    SNTP_SERVERS;           // SNTP servers from rippled.cfg.

    enum StartUpType
    {
        FRESH,
        NORMAL,
        LOAD,
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
    int                         PEER_PORT;
    int                         NUMBER_CONNECTIONS;
    std::string                 PEER_SSL_CIPHER_LIST;
    int                         PEER_SCAN_INTERVAL_MIN;
    int                         PEER_START_MAX;
    unsigned int                PEER_CONNECT_LOW_WATER;
    bool                        PEER_PRIVATE;           // True to ask peers not to relay current IP.

    // Websocket networking parameters
    std::string                 WEBSOCKET_PUBLIC_IP;        // XXX Going away. Merge with the inbound peer connction.
    int                         WEBSOCKET_PUBLIC_PORT;
    int                         WEBSOCKET_PUBLIC_SECURE;

    std::string                 WEBSOCKET_IP;
    int                         WEBSOCKET_PORT;
    int                         WEBSOCKET_SECURE;

    int                         WEBSOCKET_PING_FREQ;

    std::string                 WEBSOCKET_SSL_CERT;
    std::string                 WEBSOCKET_SSL_CHAIN;
    std::string                 WEBSOCKET_SSL_KEY;

    // RPC parameters
    std::string                 RPC_IP;
    int                         RPC_PORT;
    std::vector<std::string>    RPC_ADMIN_ALLOW;
    std::string                 RPC_ADMIN_PASSWORD;
    std::string                 RPC_ADMIN_USER;
    std::string                 RPC_PASSWORD;
    std::string                 RPC_USER;
    bool                        RPC_ALLOW_REMOTE;
    Json::Value                 RPC_STARTUP;

    int                         RPC_SECURE;
    std::string                 RPC_SSL_CERT;
    std::string                 RPC_SSL_CHAIN;
    std::string                 RPC_SSL_KEY;

    // Path searching
    int                         PATH_SEARCH_SIZE;

    // Validation
    RippleAddress               VALIDATION_SEED, VALIDATION_PUB, VALIDATION_PRIV;

    // Node/Cluster
    std::vector<std::string>    CLUSTER_NODES;
    RippleAddress               NODE_SEED, NODE_PUB, NODE_PRIV;

    // Fee schedule (All below values are in fee units)
    uint64                      FEE_DEFAULT;            // Default fee.
    uint64                      FEE_ACCOUNT_RESERVE;    // Amount of units not allowed to send.
    uint64                      FEE_OWNER_RESERVE;      // Amount of units not allowed to send per owner entry.
    uint64                      FEE_NICKNAME_CREATE;    // Fee to create a nickname.
    uint64                      FEE_OFFER;              // Rate per day.
    int                         FEE_CONTRACT_OPERATION; // fee for each contract operation

    // Node storage configuration
    uint32                      LEDGER_HISTORY;
    int                         NODE_SIZE;

    // Client behavior
    int                         ACCOUNT_PROBE_MAX;      // How far to scan for accounts.

    // Signing signatures.
    uint32                      SIGN_TRANSACTION;
    uint32                      SIGN_VALIDATION;
    uint32                      SIGN_PROPOSAL;

    boost::asio::ssl::context   SSL_CONTEXT;            // Generic SSL context.
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
    void setup (const std::string& strConf, bool bTestNet, bool bQuiet);
    void load ();
};

extern Config theConfig;

#endif

// vim:ts=4
