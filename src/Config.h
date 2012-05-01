#ifndef __CONFIG__
#define __CONFIG__

#include <string>

#define SYSTEM_NAME		"newcoin"
#define VALIDATORS_SITE		"redstem.com"

#define VALIDATORS_FILE_NAME	"validators.txt"
const int SYSTEM_PEER_PORT=6561;

// Allow anonymous DH.
#define DEFAULT_PEER_SSL_CIPHER_LIST	"ALL:!LOW:!EXP:!MD5:@STRENGTH"

// 1 hour.
#define DEFAULT_PEER_SCAN_INTERVAL_MIN	(60*60)

// Maximum number of peers to try to connect to as client at once.
#define DEFAULT_PEER_START_MAX		5

// Might connect with fewer for testing.
#define	DEFAULT_PEER_CONNECT_LOW_WATER	4

class Config
{
public:
	// core software parameters
	int VERSION;
	std::string VERSION_STR;

	// network parameters
	int NETWORK_START_TIME;		// The Unix time we start ledger 0
	int TRANSACTION_FEE_BASE;
	int LEDGER_SECONDS;
	int LEDGER_PROPOSAL_DELAY_SECONDS;
	int LEDGER_AVALANCHE_SECONDS;
	int BELIEF_QUORUM;
	float BELIEF_PERCENT;

	// node networking parameters
	std::string PEER_IP;
	int PEER_PORT;
	int NUMBER_CONNECTIONS;
//	bool NODE_INBOUND;		// we accept inbound connections
//	bool NODE_DATABASE;		// we offer historical data services
//	bool NODE_PUBLIC;		// we do not attempt to hide our identity
//	bool NODE_DUMB;			// we are a 'dumb' client
//	bool NODE_SMART;		// we offer services to 'dumb' clients

	// RPC parameters
	std::string RPC_IP;
	int RPC_PORT;
	std::string RPC_USER;
	std::string RPC_PASSWORD;

	std::string VALIDATION_PASSWORD;
	std::string VALIDATION_KEY;

	std::string PEER_SSL_CIPHER_LIST;
	int	    PEER_SCAN_INTERVAL_MIN;
	int	    PEER_START_MAX;
	int	    PEER_CONNECT_LOW_WATER;

	// configuration parameters
	std::string DATA_DIR;

	Config();

	void load();
};

extern Config theConfig;
#endif
