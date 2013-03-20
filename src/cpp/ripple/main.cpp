
#include <iostream>

#include <boost/asio.hpp>
#include <boost/foreach.hpp>
#include <boost/program_options.hpp>
#include <boost/test/included/unit_test.hpp>

#include "Application.h"
#include "CallRPC.h"
#include "Config.h"
#include "Log.h"
#include "RPCHandler.h"
#include "utils.h"

namespace po = boost::program_options;

extern bool AddSystemEntropy();
extern void TFInit();
extern void LEFInit();
extern void SVFInit();

using namespace std;
using namespace boost::unit_test;

void setupServer()
{
	theApp = new Application();
	theApp->setup();
}

void startServer()
{
	//
	// Execute start up rpc commands.
	//
	if (theConfig.RPC_STARTUP.isArray())
	{
		for (int i = 0; i != theConfig.RPC_STARTUP.size(); ++i)
		{
			const Json::Value& jvCommand	= theConfig.RPC_STARTUP[i];

			if (!theConfig.QUIET)
				std::cerr << "Startup RPC: " << jvCommand << std::endl;

			RPCHandler	rhHandler(&theApp->getOPs());

			int cost = 10;
			Json::Value	jvResult	= rhHandler.doCommand(jvCommand, RPCHandler::ADMIN, cost);

			if (!theConfig.QUIET)
				std::cerr << "Result: " << jvResult << std::endl;
		}
	}

	theApp->run();					// Blocks till we get a stop RPC.
}

bool init_unit_test()
{
	theApp = new Application();

    return true;
}

void printHelp(const po::options_description& desc)
{
	cerr << SYSTEM_NAME "d [options] <command> <params>" << endl;

	cerr << desc << endl;

	cerr << "Commands: " << endl;
	cerr << "     account_info <account>|<nickname>" << endl;
	cerr << "     account_info <seed>|<pass_phrase>|<key> [<index>]" << endl;
	cerr << "     account_lines <account>|<nickname>|<account_public_key> [<index>]" << endl;
	cerr << "     account_offers <account>|<nickname>|<account_public_key> [<index>]" << endl;
	cerr << "     account_tx <account>|<nickname>|<account_public_key> <ledger>|(<minledger> <maxledger>)" << endl;
	cerr << "     book_offers <taker_pays> <taker_gets> [<taker [<ledger> [<limit> [<proof> [<marker>]]]]]" << endl;
	cerr << "     connect <ip> [<port>]" << endl;
	cerr << "     consensus_info" << endl;
#if ENABLE_INSECURE
	cerr << "     data_delete <key>" << endl;
	cerr << "     data_fetch <key>" << endl;
	cerr << "     data_store <key> <value>" << endl;
#endif
	cerr << "     get_counts" << endl;
	cerr << "     ledger [<id>|current|closed|validated] [full]" << endl;
	cerr << "     ledger_accept" << endl;
	cerr << "     ledger_closed" << endl;
	cerr << "     ledger_current" << endl;
	cerr << "     ledger_header <ledger>" << endl;
	cerr << "     logrotate " << endl;
	cerr << "     peers" << endl;
	cerr << "     random" << endl;
	cerr << "     ripple ..." << endl;
	cerr << "     ripple_path_find <json> [<ledger>]" << endl;
//	cerr << "     send <seed> <paying_account> <account_id> <amount> [<currency>] [<send_max>] [<send_currency>]" << endl;
	cerr << "     stop" << endl;
	cerr << "     tx <id>" << endl;
	cerr << "     unl_add <domain>|<public> [<comment>]" << endl;
	cerr << "     unl_delete <domain>|<public_key>" << endl;
	cerr << "     unl_list" << endl;
	cerr << "     unl_load" << endl;
	cerr << "     unl_network" << endl;
	cerr << "     unl_reset" << endl;
	cerr << "     validation_create [<seed>|<pass_phrase>|<key>]" << endl;
	cerr << "     validation_seed [<seed>|<pass_phrase>|<key>]" << endl;
	cerr << "     wallet_add <regular_seed> <paying_account> <master_seed> [<initial_funds>] [<account_annotation>]" << endl;
	cerr << "     wallet_accounts <seed>" << endl;
	cerr << "     wallet_claim <master_seed> <regular_seed> [<source_tag>] [<account_annotation>]" << endl;
	cerr << "     wallet_seed [<seed>|<passphrase>|<passkey>]" << endl;
	cerr << "     wallet_propose [<passphrase>]" << endl;

// Transaction helpers (that were removed):
//	cerr << "     account_domain_set <seed> <paying_account> [<domain>]" << endl;
//	cerr << "     account_email_set <seed> <paying_account> [<email_address>]" << endl;
//	cerr << "     account_rate_set <seed> <paying_account> <rate>" << endl;
//	cerr << "     account_wallet_set <seed> <paying_account> [<wallet_hash>]" << endl;
//	cerr << "     nickname_info <nickname>" << endl;
//	cerr << "     nickname_set <seed> <paying_account> <nickname> [<offer_minimum>] [<authorization>]" << endl;
//	cerr << "     offer_create <seed> <paying_account> <taker_pays_amount> <taker_pays_currency> <taker_pays_issuer> <takers_gets_amount> <takers_gets_currency> <takers_gets_issuer> <expires> [passive]" << endl;
//	cerr << "     offer_cancel <seed> <paying_account> <sequence>" << endl;
//	cerr << "     password_fund <seed> <paying_account> [<account>]" << endl;
//	cerr << "     password_set <master_seed> <regular_seed> [<account>]" << endl;
//	cerr << "     trust_set <seed> <paying_account> <destination_account> <limit_amount> <currency> [<quality_in>] [<quality_out>]" << endl;
}

int main(int argc, char* argv[])
{
	NameThread("main");
	int					iResult	= 0;
	po::variables_map	vm;										// Map of options.

	//
	// Set up option parsing.
	//
	po::options_description desc("General Options");
	desc.add_options()
		("help,h", "Display this message.")
		("conf", po::value<std::string>(), "Specify the configuration file.")
		("rpc", "Perform rpc command (default).")
		("standalone,a", "Run with no peers.")
		("testnet,t", "Run in test net mode.")
		("unittest,u", "Perform unit tests.")
		("parameters", po::value< vector<string> >(), "Specify comma separated parameters.")
		("quiet,q", "Reduce diagnotics.")
		("verbose,v", "Verbose logging.")
		("load", "Load the current ledger from the local DB.")
		("ledger", po::value<std::string>(), "Load the specified ledger and start from .")
		("start", "Start from a fresh Ledger.")
		("net", "Get the initial ledger from the network.")
		("fg", "Run in the foreground.")
	;

	// Interpret positional arguments as --parameters.
	po::positional_options_description p;
	p.add("parameters", -1);

	//
	// Prepare to run
	//

	if (!AddSystemEntropy())
	{
		std::cerr << "Unable to add system entropy" << std::endl;
		iResult	= 2;
	}

	if (iResult)
	{
		nothing();
	}
	else
	{
		// Parse options, if no error.
		try {
			po::store(po::command_line_parser(argc, argv)
				.options(desc)											// Parse options.
				.positional(p)											// Remainder as --parameters.
				.run(),
				vm);
			po::notify(vm);												// Invoke option notify functions.
		}
		catch (...)
		{
			iResult	= 1;
		}
	}

	if (HaveSustain() && !vm.count("parameters") && !vm.count("fg") && !vm.count("standalone"))
	{
		std::string logMe = DoSustain();
		if (!logMe.empty())
			Log(lsWARNING) << logMe;
	}

	if (vm.count("quiet"))
		Log::setMinSeverity(lsFATAL, true);
	else if (vm.count("verbose"))
		Log::setMinSeverity(lsTRACE, true);
	else
		Log::setMinSeverity(lsINFO, true);

	InstanceType::multiThread();

	TFInit();
	LEFInit();
	SVFInit();

	if (vm.count("unittest"))
	{
		unit_test_main(init_unit_test, argc, argv);

		return 0;
	}

	if (!iResult)
	{
		theConfig.setup(
			vm.count("conf") ? vm["conf"].as<std::string>() : "",	// Config file.
			!!vm.count("testnet"),									// Testnet flag.
			!!vm.count("quiet"));									// Quiet flag.

		if (vm.count("standalone"))
		{
			theConfig.RUN_STANDALONE = true;
			theConfig.LEDGER_HISTORY = 0;
		}
	}

	if (vm.count("start")) theConfig.START_UP = Config::FRESH;
	if (vm.count("ledger"))
	{
		theConfig.START_LEDGER = vm["ledger"].as<std::string>();
		theConfig.START_UP = Config::LOAD;
	}
	else if (vm.count("load"))
	{
		theConfig.START_UP = Config::LOAD;
	}
	else if (vm.count("net"))
	{
		theConfig.START_UP = Config::NETWORK;
		if (theConfig.VALIDATION_QUORUM < 2)
			theConfig.VALIDATION_QUORUM = 2;
	}

	if (iResult)
	{
		nothing();
	}
	else if (vm.count("help"))
	{
		iResult	= 1;
	}
	else if (!vm.count("parameters"))
	{
		// No arguments. Run server.
		setupServer();
		NameThread("io");
		startServer();
	}
	else
	{
		// Have a RPC command.
		NameThread("rpc");
		std::vector<std::string> vCmd	= vm["parameters"].as<std::vector<std::string> >();

		iResult	= commandLineRPC(vCmd);
	}

	if (1 == iResult && !vm.count("quiet"))
		printHelp(desc);

	return iResult;
}
// vim:ts=4
