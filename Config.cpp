#include "Config.h"
#include "util/pugixml.hpp"

#include <boost/lexical_cast.hpp>

using namespace pugi;

Config theConfig;

Config::Config()
{
	VERSION=1;

	NETWORK_START_TIME=1319844908;


	PEER_PORT=6561;
	RPC_PORT=5001;
	NUMBER_CONNECTIONS=30;
	
	// a new ledger every 30 min
	LEDGER_SECONDS=(60*30); 

	RPC_USER="admin";
	RPC_PASSWORD="pass";

	DATA_DIR="";

	TRANSACTION_FEE_BASE=1000;
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

	/*
	node=root.child("DB_TYPE");
	if(!node.empty())
	{
		if( stricmp(node.child_value(),"mysql")==0 ) theApp->setDB(Database::newMysqlDatabase("host","user","pass"));
		else theApp->setSerializer(new DiskSerializer());
	}else */

}
