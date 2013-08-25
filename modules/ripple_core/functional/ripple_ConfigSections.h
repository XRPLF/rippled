//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CONFIGSECTIONS_H_INCLUDED
#define RIPPLE_CONFIGSECTIONS_H_INCLUDED

// VFALCO NOTE
//
//      Please use this style for all new sections
//      And if you're feeling generous, convert all the
//      existing macros to this format as well.
//
struct ConfigSection
{
    static String nodeDatabase ()                 { return "node_db"; }
    static String tempNodeDatabase ()             { return "temp_db"; }
    static String importNodeDatabase ()           { return "import_db"; }
};

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
#define SECTION_LEDGER_HISTORY          "ledger_history"
#define SECTION_IPS                     "ips"
#define SECTION_NETWORK_QUORUM          "network_quorum"
#define SECTION_NODE_SEED               "node_seed"
#define SECTION_NODE_SIZE               "node_size"
#define SECTION_PATH_SEARCH_SIZE        "path_search_size"
#define SECTION_PEER_CONNECT_LOW_WATER  "peer_connect_low_water"
#define SECTION_PEER_IP                 "peer_ip"
#define SECTION_PEER_PORT               "peer_port"
#define SECTION_PEER_PROXY_PORT         "peer_port_proxy"
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

#endif
