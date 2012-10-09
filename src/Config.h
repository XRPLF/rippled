#ifndef __CONFIG__
#define __CONFIG__

#include "types.h"
#include "NewcoinAddress.h"
#include "ParseSection.h"
#include "SerializedTypes.h"

#include <string>
#include <boost/filesystem.hpp>

#define SYSTEM_NAME					"newcoin"
#define SYSTEM_CURRENCY_CODE		"XNS"
#define SYSTEM_CURRENCY_PRECISION	6
#define SYSTEM_CURRENCY_CODE_RIPPLE	"XNR"

#define SYSTEM_CURRENCY_GIFT		1000ull
#define SYSTEM_CURRENCY_USERS		100000000ull
#define SYSTEM_CURRENCY_PARTS		1000000ull		// 10^SYSTEM_CURRENCY_PRECISION
#define SYSTEM_CURRENCY_START		(SYSTEM_CURRENCY_GIFT*SYSTEM_CURRENCY_USERS*SYSTEM_CURRENCY_PARTS)

#define CONFIG_FILE_NAME				SYSTEM_NAME "d.cfg"	// newcoind.cfg

#define DEFAULT_VALIDATORS_SITE		"redstem.com"
#define VALIDATORS_FILE_NAME		"validators.txt"

const int SYSTEM_PEER_PORT			= 6561;
const int SYSTEM_WEBSOCKET_PORT		= 6562;

// Allow anonymous DH.
#define DEFAULT_PEER_SSL_CIPHER_LIST	"ALL:!LOW:!EXP:!MD5:@STRENGTH"

// Normal, recommend 1 hour.
// #define DEFAULT_PEER_SCAN_INTERVAL_MIN	(60*60)
// Testing, recommend 1 minute.
#define DEFAULT_PEER_SCAN_INTERVAL_MIN	(60)

// Maximum number of peers to try to connect to as client at once.
#define DEFAULT_PEER_START_MAX			5

// Might connect with fewer for testing.
#define	DEFAULT_PEER_CONNECT_LOW_WATER	4

class Config
{
public:
	// Configuration parameters
	boost::filesystem::path		CONFIG_FILE;
	boost::filesystem::path		CONFIG_DIR;
	boost::filesystem::path		DATA_DIR;
	boost::filesystem::path		DEBUG_LOGFILE;
	boost::filesystem::path		UNL_DEFAULT;

	std::string					VALIDATORS_SITE;		// Where to find validators.txt on the Internet.
	std::vector<std::string>	VALIDATORS;				// Validators from newcoind.cfg.
	std::vector<std::string>	IPS;					// Peer IPs from newcoind.cfg.
	std::vector<std::string>	SNTP_SERVERS;			// SNTP servers from newcoind.cfg.

	enum StartUpType {FRESH,NORMAL,LOAD};
	StartUpType					START_UP;

	// Network parameters
	int							NETWORK_START_TIME;		// The Unix time we start ledger 0.
	int							TRANSACTION_FEE_BASE;
	int							LEDGER_SECONDS;
	int							LEDGER_PROPOSAL_DELAY_SECONDS;
	int							LEDGER_AVALANCHE_SECONDS;
	bool						LEDGER_CREATOR;     // should be false unless we are starting a new ledger
	bool						RUN_STANDALONE;

	// Note: The following parameters do not relate to the UNL or trust at all
	unsigned int				NETWORK_QUORUM;			// Minimum number of nodes to consider the network present
	int							VALIDATION_QUORUM;		// Minimum validations to consider ledger authoritative

	// Peer networking parameters
	std::string					PEER_IP;
	int							PEER_PORT;
	int							NUMBER_CONNECTIONS;
	std::string					PEER_SSL_CIPHER_LIST;
	int							PEER_SCAN_INTERVAL_MIN;
	int							PEER_START_MAX;
	unsigned int				PEER_CONNECT_LOW_WATER;

	// Websocket networking parameters
	std::string					WEBSOCKET_IP;
	int							WEBSOCKET_PORT;

	// RPC parameters
	std::string					RPC_IP;
	int							RPC_PORT;
	std::string					RPC_USER;
	std::string					RPC_PASSWORD;
	bool						RPC_ALLOW_REMOTE;

	// Validation
	NewcoinAddress				VALIDATION_SEED;

	// Fees
	uint64						FEE_DEFAULT;			// Default fee.
	uint64						FEE_ACCOUNT_CREATE;		// Fee to create an account.
	uint64						FEE_NICKNAME_CREATE;	// Fee to create a nickname.
	uint64						FEE_OFFER;				// Rate per day.
	int							FEE_CONTRACT_OPERATION; // fee for each contract operation

	// Client behavior
	int							ACCOUNT_PROBE_MAX;		// How far to scan for accounts.

	void setup(const std::string& strConf);
	void load();
};

extern Config theConfig;
#endif

// vim:ts=4
