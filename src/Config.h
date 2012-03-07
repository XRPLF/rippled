#include "string"

class Config
{
public:

	// core software parameters
	int VERSION;
	std::string VERSION_STR;

	// network parameters
	std::string NETWORK_ID;
	std::string NETWORK_DNS_SEEDS;
	int NETWORK_START_TIME;  // The Unix time we start ledger 0
	int TRANSACTION_FEE_BASE;
	int LEDGER_SECONDS;
	int LEDGER_PROPOSAL_DELAY_SECONDS;
	int LEDGER_AVALANCHE_SECONDS;
	int BELIEF_QUORUM;
	float BELIEF_PERCENT;

	// node networking parameters
	int PEER_PORT;
	int NUMBER_CONNECTIONS;
	bool NODE_INBOUND;		// we accept inbound connections
	bool NODE_DATABASE;		// we offer historical data services
	bool NODE_PUBLIC;		// we do not attempt to hide our identity
	bool NODE_DUMB;			// we are a 'dumb' client
	bool NODE_SMART;		// we offer services to 'dumb' clients

	std::string HANKO_PRIVATE;

	// RPC parameters
	int RPC_PORT;
	std::string RPC_USER;
	std::string RPC_PASSWORD;

	// configuration parameters
	std::string DATA_DIR;

	Config();

	void load();
};

extern Config theConfig;
