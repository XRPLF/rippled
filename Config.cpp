#include "Config.h"
#include "util/pugixml.hpp"
#include <boost/lexical_cast.hpp>

using namespace pugi;

Config theConfig;

Config::Config()
{
	VERSION=1;
	TEST_NET=false;

	PEER_PORT=6561;
	RPC_PORT=5001;
	NUMBER_CONNECTIONS=30;
	
	// a new ledger every 30 min
	LEDGER_SECONDS=(60*30); 

	// length of delay between start finalization and sending your first proposal
	// This delay allows us to collect a few extra transactions from people who's clock is different than ours
	// It should increase the chance that the ledgers will all hash the same
	LEDGER_PROPOSAL_DELAY_SECONDS=30; 

	// How long to wait between proposal send and ledger close. 
	// at which point you publish your validation
	// You are only waiting to get extra transactions from your peers 
	LEDGER_FINALIZATION_SECONDS=(60*5); 
	RPC_USER="admin";
	RPC_PASSWORD="pass";

	HISTORY_DIR="history/";

	TRANSACTION_FEE=1000;
	ACCOUNT_FEE=1000;
}

void Config::load()
{
	
	xml_document doc;
	xml_parse_result result = doc.load_file("config.xml");
	xml_node root=doc.child("config");

	xml_node node= root.child("PEER_PORT");
	if(!node.empty()) PEER_PORT=boost::lexical_cast<int>(node.child_value());

	node= root.child("RPC_PORT");
	if(!node.empty()) RPC_PORT=boost::lexical_cast<int>(node.child_value());


}