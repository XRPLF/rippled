#include "string"

class Config
{
public:
	int VERSION;
	std::string VERSION_STR;
	bool TEST_NET;

	int TRANSACTION_FEE;
	int ACCOUNT_FEE;
	int PEER_PORT;
	int RPC_PORT;
	int NUMBER_CONNECTIONS;
	int LEDGER_SECONDS;
	int LEDGER_PROPOSAL_DELAY_SECONDS;
	int LEDGER_FINALIZATION_SECONDS;
	std::string RECEIVE_ACTION;
	int BELIEF_QUORUM;
	float BELIEF_PERCENT;
	std::string RPC_USER;
	std::string RPC_PASSWORD;
	std::string HANKO;

	std::string HISTORY_DIR;



	Config();

	void load();
};

extern Config theConfig;