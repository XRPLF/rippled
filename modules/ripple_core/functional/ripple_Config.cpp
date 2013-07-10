//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

//
// TODO: Check permissions on config file before using it.
//

// VFALCO TODO Rename and replace these macros with variables.
#define SECTION_ACCOUNT_PROBE_MAX       "account_probe_max"
#define SECTION_CLUSTER_NODES           "cluster_nodes"
#define SECTION_DATABASE_PATH           "database_path"
#define SECTION_DEBUG_LOGFILE           "debug_logfile"
#define SECTION_ELB_SUPPORT             "elb_support"
#define SECTION_FEE_DEFAULT             "fee_default"
#define SECTION_FEE_NICKNAME_CREATE     "fee_nickname_create"
#define SECTION_FEE_OFFER               "fee_offer"
#define SECTION_FEE_OPERATION           "fee_operation"
#define SECTION_FEE_ACCOUNT_RESERVE     "fee_account_reserve"
#define SECTION_FEE_OWNER_RESERVE       "fee_owner_reserve"
#define SECTION_NODE_DB                 "node_db"
#define SECTION_FASTNODE_DB             "temp_db"
#define SECTION_LEDGER_HISTORY          "ledger_history"
#define SECTION_IPS                     "ips"
#define SECTION_NETWORK_QUORUM          "network_quorum"
#define SECTION_NODE_SEED               "node_seed"
#define SECTION_NODE_SIZE               "node_size"
#define SECTION_PATH_SEARCH_SIZE        "path_search_size"
#define SECTION_PEER_CONNECT_LOW_WATER  "peer_connect_low_water"
#define SECTION_PEER_IP                 "peer_ip"
#define SECTION_PEER_PORT               "peer_port"
#define SECTION_PEER_PRIVATE            "peer_private"
#define SECTION_PEER_SCAN_INTERVAL_MIN  "peer_scan_interval_min"
#define SECTION_PEER_SSL_CIPHER_LIST    "peer_ssl_cipher_list"
#define SECTION_PEER_START_MAX          "peer_start_max"
#define SECTION_RPC_ALLOW_REMOTE        "rpc_allow_remote"
#define SECTION_RPC_ADMIN_ALLOW         "rpc_admin_allow"
#define SECTION_RPC_ADMIN_USER          "rpc_admin_user"
#define SECTION_RPC_ADMIN_PASSWORD      "rpc_admin_password"
#define SECTION_RPC_IP                  "rpc_ip"
#define SECTION_RPC_PORT                "rpc_port"
#define SECTION_RPC_USER                "rpc_user"
#define SECTION_RPC_PASSWORD            "rpc_password"
#define SECTION_RPC_STARTUP             "rpc_startup"
#define SECTION_RPC_SECURE              "rpc_secure"
#define SECTION_RPC_SSL_CERT            "rpc_ssl_cert"
#define SECTION_RPC_SSL_CHAIN           "rpc_ssl_chain"
#define SECTION_RPC_SSL_KEY             "rpc_ssl_key"
#define SECTION_SMS_FROM                "sms_from"
#define SECTION_SMS_KEY                 "sms_key"
#define SECTION_SMS_SECRET              "sms_secret"
#define SECTION_SMS_TO                  "sms_to"
#define SECTION_SMS_URL                 "sms_url"
#define SECTION_SNTP                    "sntp_servers"
#define SECTION_SSL_VERIFY              "ssl_verify"
#define SECTION_SSL_VERIFY_FILE         "ssl_verify_file"
#define SECTION_SSL_VERIFY_DIR          "ssl_verify_dir"
#define SECTION_VALIDATORS_FILE         "validators_file"
#define SECTION_VALIDATION_QUORUM       "validation_quorum"
#define SECTION_VALIDATION_SEED         "validation_seed"
#define SECTION_WEBSOCKET_PUBLIC_IP     "websocket_public_ip"
#define SECTION_WEBSOCKET_PUBLIC_PORT   "websocket_public_port"
#define SECTION_WEBSOCKET_PUBLIC_SECURE "websocket_public_secure"
#define SECTION_WEBSOCKET_PING_FREQ     "websocket_ping_frequency"
#define SECTION_WEBSOCKET_IP            "websocket_ip"
#define SECTION_WEBSOCKET_PORT          "websocket_port"
#define SECTION_WEBSOCKET_SECURE        "websocket_secure"
#define SECTION_WEBSOCKET_SSL_CERT      "websocket_ssl_cert"
#define SECTION_WEBSOCKET_SSL_CHAIN     "websocket_ssl_chain"
#define SECTION_WEBSOCKET_SSL_KEY       "websocket_ssl_key"
#define SECTION_VALIDATORS              "validators"
#define SECTION_VALIDATORS_SITE         "validators_site"

// Fees are in XRP.
#define DEFAULT_FEE_DEFAULT             10
#define DEFAULT_FEE_ACCOUNT_RESERVE     200*SYSTEM_CURRENCY_PARTS
#define DEFAULT_FEE_OWNER_RESERVE       50*SYSTEM_CURRENCY_PARTS
#define DEFAULT_FEE_NICKNAME_CREATE     1000
#define DEFAULT_FEE_OFFER               DEFAULT_FEE_DEFAULT
#define DEFAULT_FEE_OPERATION           1

Config theConfig;

void Config::setup (const std::string& strConf, bool bTestNet, bool bQuiet)
{
    boost::system::error_code   ec;
    std::string                 strDbPath, strConfFile;

    //
    // Determine the config and data directories.
    // If the config file is found in the current working directory, use the current working directory as the config directory and
    // that with "db" as the data directory.
    //

    TESTNET     = bTestNet;
    QUIET       = bQuiet;
    NODE_SIZE   = 0;

    // TESTNET forces a "testnet-" prefix on the conf file and db directory.
    strDbPath           = TESTNET ? "testnet-db" : "db";
    strConfFile         = boost::str (boost::format (TESTNET ? "testnet-%s" : "%s")
                                      % (strConf.empty () ? CONFIG_FILE_NAME : strConf));

    VALIDATORS_BASE     = boost::str (boost::format (TESTNET ? "testnet-%s" : "%s")
                                      % VALIDATORS_FILE_NAME);
    VALIDATORS_URI      = boost::str (boost::format ("/%s") % VALIDATORS_BASE);

    if (TESTNET)
    {
        SIGN_TRANSACTION    = HashPrefix::txSignTestnet;
        SIGN_VALIDATION     = HashPrefix::validationTestnet;
        SIGN_PROPOSAL       = HashPrefix::proposalTestnet;
    }
    else
    {
        SIGN_TRANSACTION    = HashPrefix::txSign;
        SIGN_VALIDATION     = HashPrefix::validation;
        SIGN_PROPOSAL       = HashPrefix::proposal;
    }

    if (TESTNET)
        Base58::setCurrentAlphabet (Base58::getTestnetAlphabet ());

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

        if (exists (CONFIG_FILE)
                // Can we figure out XDG dirs?
                || (!getenv ("HOME") && (!getenv ("XDG_CONFIG_HOME") || !getenv ("XDG_DATA_HOME"))))
        {
            // Current working directory is fine, put dbs in a subdir.
            nothing ();
        }
        else
        {
            // Construct XDG config and data home.
            // http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html
            std::string strHome             = strGetEnv ("HOME");
            std::string strXdgConfigHome    = strGetEnv ("XDG_CONFIG_HOME");
            std::string strXdgDataHome      = strGetEnv ("XDG_DATA_HOME");

            if (strXdgConfigHome.empty ())
            {
                // $XDG_CONFIG_HOME was not set, use default based on $HOME.
                strXdgConfigHome    = boost::str (boost::format ("%s/.config") % strHome);
            }

            if (strXdgDataHome.empty ())
            {
                // $XDG_DATA_HOME was not set, use default based on $HOME.
                strXdgDataHome  = boost::str (boost::format ("%s/.local/share") % strHome);
            }

            CONFIG_DIR          = boost::str (boost::format ("%s/" SYSTEM_NAME) % strXdgConfigHome);
            CONFIG_FILE         = CONFIG_DIR / strConfFile;
            DATA_DIR            = boost::str (boost::format ("%s/" SYSTEM_NAME) % strXdgDataHome);

            boost::filesystem::create_directories (CONFIG_DIR, ec);

            if (ec)
                throw std::runtime_error (boost::str (boost::format ("Can not create %s") % CONFIG_DIR));
        }
    }


    if (SSL_VERIFY_FILE.empty ())
    {
        SSL_CONTEXT.set_default_verify_paths (ec);

        if (ec && SSL_VERIFY_DIR.empty ())
            throw std::runtime_error (boost::str (boost::format ("Failed to set_default_verify_paths: %s") % ec.message ()));
    }
    else
        SSL_CONTEXT.load_verify_file (SSL_VERIFY_FILE);

    if (!SSL_VERIFY_DIR.empty ())
    {
        SSL_CONTEXT.add_verify_path (SSL_VERIFY_DIR, ec);

        if (ec)
            throw std::runtime_error (boost::str (boost::format ("Failed to add verify path: %s") % ec.message ()));
    }

    // Update default values
    load ();

    // Log::out() << "CONFIG FILE: " << CONFIG_FILE;
    // Log::out() << "CONFIG DIR: " << CONFIG_DIR;
    // Log::out() << "DATA DIR: " << DATA_DIR;

    boost::filesystem::create_directories (DATA_DIR, ec);

    if (ec)
        throw std::runtime_error (boost::str (boost::format ("Can not create %s") % DATA_DIR));
}

Config::Config ()
    : m_rpcPort (5001)
    , SSL_CONTEXT (boost::asio::ssl::context::sslv23)
{
    //
    // Defaults
    //

    TESTNET                 = false;
    NETWORK_START_TIME      = 1319844908;

    PEER_PORT               = SYSTEM_PEER_PORT;
    RPC_SECURE              = 0;
    WEBSOCKET_PORT          = SYSTEM_WEBSOCKET_PORT;
    WEBSOCKET_PUBLIC_PORT   = SYSTEM_WEBSOCKET_PUBLIC_PORT;
    WEBSOCKET_PUBLIC_SECURE = 1;
    WEBSOCKET_SECURE        = 0;
    WEBSOCKET_PING_FREQ     = (5 * 60);
    NUMBER_CONNECTIONS      = 30;

    // a new ledger every minute
    LEDGER_SECONDS          = 60;
    LEDGER_CREATOR          = false;

    RPC_ALLOW_REMOTE        = false;
    RPC_ADMIN_ALLOW.push_back ("127.0.0.1");

    PEER_SSL_CIPHER_LIST    = DEFAULT_PEER_SSL_CIPHER_LIST;
    PEER_SCAN_INTERVAL_MIN  = DEFAULT_PEER_SCAN_INTERVAL_MIN;

    PEER_START_MAX          = DEFAULT_PEER_START_MAX;
    PEER_CONNECT_LOW_WATER  = DEFAULT_PEER_CONNECT_LOW_WATER;

    PEER_PRIVATE            = false;

    TRANSACTION_FEE_BASE    = DEFAULT_FEE_DEFAULT;

    NETWORK_QUORUM          = 0;    // Don't need to see other nodes
    VALIDATION_QUORUM       = 1;    // Only need one node to vouch

    FEE_ACCOUNT_RESERVE     = DEFAULT_FEE_ACCOUNT_RESERVE;
    FEE_OWNER_RESERVE       = DEFAULT_FEE_OWNER_RESERVE;
    FEE_NICKNAME_CREATE     = DEFAULT_FEE_NICKNAME_CREATE;
    FEE_OFFER               = DEFAULT_FEE_OFFER;
    FEE_DEFAULT             = DEFAULT_FEE_DEFAULT;
    FEE_CONTRACT_OPERATION  = DEFAULT_FEE_OPERATION;

    LEDGER_HISTORY          = 256;

    PATH_SEARCH_SIZE        = DEFAULT_PATH_SEARCH_SIZE;
    ACCOUNT_PROBE_MAX       = 10;

    VALIDATORS_SITE         = DEFAULT_VALIDATORS_SITE;

    SSL_VERIFY              = true;

    ELB_SUPPORT             = false;
    RUN_STANDALONE          = false;
    START_UP                = NORMAL;
}

void Config::load ()
{
    if (!QUIET)
        Log::out() << "Loading: " << CONFIG_FILE;

    std::ifstream   ifsConfig (CONFIG_FILE.c_str (), std::ios::in);

    if (!ifsConfig)
    {
        Log::out() << "Failed to open '" << CONFIG_FILE << "'.";
    }
    else
    {
        std::string strConfigFile;

        strConfigFile.assign ((std::istreambuf_iterator<char> (ifsConfig)),
                              std::istreambuf_iterator<char> ());

        if (ifsConfig.bad ())
        {
            Log::out() << "Failed to read '" << CONFIG_FILE << "'.";
        }
        else
        {
            Section     secConfig   = ParseSection (strConfigFile, true);
            std::string strTemp;

            // XXX Leak
            Section::mapped_type*   smtTmp;

            smtTmp  = SectionEntries (secConfig, SECTION_VALIDATORS);

            if (smtTmp)
            {
                VALIDATORS  = *smtTmp;
                // SectionEntriesPrint(&VALIDATORS, SECTION_VALIDATORS);
            }

            smtTmp = SectionEntries (secConfig, SECTION_CLUSTER_NODES);

            if (smtTmp)
            {
                CLUSTER_NODES = *smtTmp;
                // SectionEntriesPrint(&CLUSTER_NODES, SECTION_CLUSTER_NODES);
            }

            smtTmp  = SectionEntries (secConfig, SECTION_IPS);

            if (smtTmp)
            {
                IPS = *smtTmp;
                // SectionEntriesPrint(&IPS, SECTION_IPS);
            }

            smtTmp = SectionEntries (secConfig, SECTION_SNTP);

            if (smtTmp)
            {
                SNTP_SERVERS = *smtTmp;
            }

            smtTmp  = SectionEntries (secConfig, SECTION_RPC_STARTUP);

            if (smtTmp)
            {
                RPC_STARTUP = Json::arrayValue;

                BOOST_FOREACH (const std::string & strJson, *smtTmp)
                {
                    Json::Reader    jrReader;
                    Json::Value     jvCommand;

                    if (!jrReader.parse (strJson, jvCommand))
                        throw std::runtime_error (boost::str (boost::format ("Couldn't parse [" SECTION_RPC_STARTUP "] command: %s") % strJson));

                    RPC_STARTUP.append (jvCommand);
                }
            }

            if (SectionSingleB (secConfig, SECTION_DATABASE_PATH, DATABASE_PATH))
                DATA_DIR    = DATABASE_PATH;


            (void) SectionSingleB (secConfig, SECTION_VALIDATORS_SITE, VALIDATORS_SITE);

            (void) SectionSingleB (secConfig, SECTION_PEER_IP, PEER_IP);

            if (SectionSingleB (secConfig, SECTION_PEER_PORT, strTemp))
                PEER_PORT           = boost::lexical_cast<int> (strTemp);

            if (SectionSingleB (secConfig, SECTION_PEER_PRIVATE, strTemp))
                PEER_PRIVATE        = boost::lexical_cast<bool> (strTemp);

            smtTmp = SectionEntries (secConfig, SECTION_RPC_ADMIN_ALLOW);

            if (smtTmp)
            {
                RPC_ADMIN_ALLOW = *smtTmp;
            }

            (void) SectionSingleB (secConfig, SECTION_RPC_ADMIN_PASSWORD, RPC_ADMIN_PASSWORD);
            (void) SectionSingleB (secConfig, SECTION_RPC_ADMIN_USER, RPC_ADMIN_USER);
            (void) SectionSingleB (secConfig, SECTION_RPC_IP, m_rpcIP);
            (void) SectionSingleB (secConfig, SECTION_RPC_PASSWORD, RPC_PASSWORD);
            (void) SectionSingleB (secConfig, SECTION_RPC_USER, RPC_USER);
            (void) SectionSingleB (secConfig, SECTION_NODE_DB, NODE_DB);
            (void) SectionSingleB (secConfig, SECTION_FASTNODE_DB, FASTNODE_DB);

            if (SectionSingleB (secConfig, SECTION_RPC_PORT, strTemp))
                m_rpcPort = boost::lexical_cast<int> (strTemp);

            if (SectionSingleB (secConfig, "ledger_creator" , strTemp))
                LEDGER_CREATOR = boost::lexical_cast<bool> (strTemp);

            if (SectionSingleB (secConfig, SECTION_RPC_ALLOW_REMOTE, strTemp))
                RPC_ALLOW_REMOTE    = boost::lexical_cast<bool> (strTemp);

            if (SectionSingleB (secConfig, SECTION_NODE_SIZE, strTemp))
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
                    NODE_SIZE = boost::lexical_cast<int> (strTemp);

                    if (NODE_SIZE < 0)
                        NODE_SIZE = 0;
                    else if (NODE_SIZE > 4)
                        NODE_SIZE = 4;
                }
            }

            if (SectionSingleB (secConfig, SECTION_ELB_SUPPORT, strTemp))
                ELB_SUPPORT         = boost::lexical_cast<bool> (strTemp);

            (void) SectionSingleB (secConfig, SECTION_WEBSOCKET_IP, WEBSOCKET_IP);

            if (SectionSingleB (secConfig, SECTION_WEBSOCKET_PORT, strTemp))
                WEBSOCKET_PORT      = boost::lexical_cast<int> (strTemp);

            (void) SectionSingleB (secConfig, SECTION_WEBSOCKET_PUBLIC_IP, WEBSOCKET_PUBLIC_IP);

            if (SectionSingleB (secConfig, SECTION_WEBSOCKET_PUBLIC_PORT, strTemp))
                WEBSOCKET_PUBLIC_PORT   = boost::lexical_cast<int> (strTemp);

            if (SectionSingleB (secConfig, SECTION_WEBSOCKET_SECURE, strTemp))
                WEBSOCKET_SECURE    = boost::lexical_cast<int> (strTemp);

            if (SectionSingleB (secConfig, SECTION_WEBSOCKET_PUBLIC_SECURE, strTemp))
                WEBSOCKET_PUBLIC_SECURE = boost::lexical_cast<int> (strTemp);

            if (SectionSingleB (secConfig, SECTION_WEBSOCKET_PING_FREQ, strTemp))
                WEBSOCKET_PING_FREQ = boost::lexical_cast<int> (strTemp);

            SectionSingleB (secConfig, SECTION_WEBSOCKET_SSL_CERT, WEBSOCKET_SSL_CERT);
            SectionSingleB (secConfig, SECTION_WEBSOCKET_SSL_CHAIN, WEBSOCKET_SSL_CHAIN);
            SectionSingleB (secConfig, SECTION_WEBSOCKET_SSL_KEY, WEBSOCKET_SSL_KEY);

            if (SectionSingleB (secConfig, SECTION_RPC_SECURE, strTemp))
                RPC_SECURE  = boost::lexical_cast<int> (strTemp);

            SectionSingleB (secConfig, SECTION_RPC_SSL_CERT, RPC_SSL_CERT);
            SectionSingleB (secConfig, SECTION_RPC_SSL_CHAIN, RPC_SSL_CHAIN);
            SectionSingleB (secConfig, SECTION_RPC_SSL_KEY, RPC_SSL_KEY);


            SectionSingleB (secConfig, SECTION_SSL_VERIFY_FILE, SSL_VERIFY_FILE);
            SectionSingleB (secConfig, SECTION_SSL_VERIFY_DIR, SSL_VERIFY_DIR);

            if (SectionSingleB (secConfig, SECTION_SSL_VERIFY, strTemp))
                SSL_VERIFY          = boost::lexical_cast<bool> (strTemp);

            if (SectionSingleB (secConfig, SECTION_VALIDATION_SEED, strTemp))
            {
                VALIDATION_SEED.setSeedGeneric (strTemp);

                if (VALIDATION_SEED.isValid ())
                {
                    VALIDATION_PUB = RippleAddress::createNodePublic (VALIDATION_SEED);
                    VALIDATION_PRIV = RippleAddress::createNodePrivate (VALIDATION_SEED);
                }
            }

            if (SectionSingleB (secConfig, SECTION_NODE_SEED, strTemp))
            {
                NODE_SEED.setSeedGeneric (strTemp);

                if (NODE_SEED.isValid ())
                {
                    NODE_PUB = RippleAddress::createNodePublic (NODE_SEED);
                    NODE_PRIV = RippleAddress::createNodePrivate (NODE_SEED);
                }
            }

            (void) SectionSingleB (secConfig, SECTION_PEER_SSL_CIPHER_LIST, PEER_SSL_CIPHER_LIST);

            if (SectionSingleB (secConfig, SECTION_PEER_SCAN_INTERVAL_MIN, strTemp))
                // Minimum for min is 60 seconds.
                PEER_SCAN_INTERVAL_MIN = std::max (60, boost::lexical_cast<int> (strTemp));

            if (SectionSingleB (secConfig, SECTION_PEER_START_MAX, strTemp))
                PEER_START_MAX      = std::max (1, boost::lexical_cast<int> (strTemp));

            if (SectionSingleB (secConfig, SECTION_PEER_CONNECT_LOW_WATER, strTemp))
                PEER_CONNECT_LOW_WATER = std::max (1, boost::lexical_cast<int> (strTemp));

            if (SectionSingleB (secConfig, SECTION_NETWORK_QUORUM, strTemp))
                NETWORK_QUORUM      = std::max (0, boost::lexical_cast<int> (strTemp));

            if (SectionSingleB (secConfig, SECTION_VALIDATION_QUORUM, strTemp))
                VALIDATION_QUORUM   = std::max (0, boost::lexical_cast<int> (strTemp));

            if (SectionSingleB (secConfig, SECTION_FEE_ACCOUNT_RESERVE, strTemp))
                FEE_ACCOUNT_RESERVE = boost::lexical_cast<uint64> (strTemp);

            if (SectionSingleB (secConfig, SECTION_FEE_OWNER_RESERVE, strTemp))
                FEE_OWNER_RESERVE   = boost::lexical_cast<uint64> (strTemp);

            if (SectionSingleB (secConfig, SECTION_FEE_NICKNAME_CREATE, strTemp))
                FEE_NICKNAME_CREATE = boost::lexical_cast<int> (strTemp);

            if (SectionSingleB (secConfig, SECTION_FEE_OFFER, strTemp))
                FEE_OFFER           = boost::lexical_cast<int> (strTemp);

            if (SectionSingleB (secConfig, SECTION_FEE_DEFAULT, strTemp))
                FEE_DEFAULT         = boost::lexical_cast<int> (strTemp);

            if (SectionSingleB (secConfig, SECTION_FEE_OPERATION, strTemp))
                FEE_CONTRACT_OPERATION  = boost::lexical_cast<int> (strTemp);

            if (SectionSingleB (secConfig, SECTION_LEDGER_HISTORY, strTemp))
            {
                boost::to_lower (strTemp);

                if (strTemp == "none")
                    LEDGER_HISTORY = 0;
                else if (strTemp == "full")
                    LEDGER_HISTORY = 1000000000u;
                else
                    LEDGER_HISTORY = boost::lexical_cast<uint32> (strTemp);
            }

            if (SectionSingleB (secConfig, SECTION_PATH_SEARCH_SIZE, strTemp))
                PATH_SEARCH_SIZE    = boost::lexical_cast<int> (strTemp);

            if (SectionSingleB (secConfig, SECTION_ACCOUNT_PROBE_MAX, strTemp))
                ACCOUNT_PROBE_MAX   = boost::lexical_cast<int> (strTemp);

            (void) SectionSingleB (secConfig, SECTION_SMS_FROM, SMS_FROM);
            (void) SectionSingleB (secConfig, SECTION_SMS_KEY, SMS_KEY);
            (void) SectionSingleB (secConfig, SECTION_SMS_SECRET, SMS_SECRET);
            (void) SectionSingleB (secConfig, SECTION_SMS_TO, SMS_TO);
            (void) SectionSingleB (secConfig, SECTION_SMS_URL, SMS_URL);

            if (SectionSingleB (secConfig, SECTION_VALIDATORS_FILE, strTemp))
                VALIDATORS_FILE     = strTemp;

            if (SectionSingleB (secConfig, SECTION_DEBUG_LOGFILE, strTemp))
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

        { siNodeCacheSize,      {   8192,   65536,  262144, 2097152,    0       } },
        { siNodeCacheAge,       {   30,     60,     90,     300,        900     } },

        { siSLECacheSize,       {   4096,   8192,   16384,  65536,      0       } },
        { siSLECacheAge,        {   30,     60,     90,     120,        300     } },

        { siLedgerSize,         {   32,     128,    256,    2048,       0       } },
        { siLedgerAge,          {   30,     90,     180,    300,        900     } },

        { siHashNodeDBCache,    {   4,      12,     24,     32,         64      } },
        { siTxnDBCache,         {   4,      12,     24,     32,         32      } },
        { siLgrDBCache,         {   4,      8,      16,     16,         16      } },
    };

    for (int i = 0; i < (sizeof (sizeTable) / sizeof (SizedItem)); ++i)
    {
        if (sizeTable[i].item == item)
            return sizeTable[i].sizes[NODE_SIZE];
    }

    assert (false);
    return -1;
}

void Config::setRpcIpAndOptionalPort (std::string const& newAddress)
{
    String const s (newAddress.c_str ());
    
    int const colonPosition = s.lastIndexOfChar (':');

    if (colonPosition != -1)
    {
        String const ipPart = s.substring (0, colonPosition);
        String const portPart = s.substring (colonPosition + 1, s.length ());

        setRpcIP (ipPart.toRawUTF8 ());
        setRpcPort (portPart.getIntValue ());
    }
    else
    {
        setRpcIP (newAddress);
    }
}

