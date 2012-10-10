#include <boost/asio.hpp>
#include <iostream>

#include <boost/foreach.hpp>
#include <boost/program_options.hpp>
#include <boost/test/included/unit_test.hpp>

#include "Application.h"
#include "CallRPC.h"
#include "Config.h"
#include "utils.h"
#include "Log.h"

namespace po = boost::program_options;

extern bool AddSystemEntropy();
using namespace std;
using namespace boost::unit_test;

void startServer()
{
	theApp = new Application();
	theApp->run();					// Blocks till we get a stop RPC.
}


bool init_unit_test()
{
	theApp = new Application();

    return true;
}

void printHelp(const po::options_description& desc)
{
	cout << "newcoin [options] <command> <params>" << endl;

	cout << desc << endl;

	cout << "Commands: " << endl;
	cout << "     account_domain_set <seed> <paying_account> [<domain>]" << endl;
	cout << "     account_email_set <seed> <paying_account> [<email_address>]" << endl;
	cout << "     account_info <account>|<nickname>" << endl;
	cout << "     account_info <seed>|<pass_phrase>|<key> [<index>]" << endl;
	cout << "     account_message_set <seed> <paying_account> <pub_key>" << endl;
	cout << "     account_publish_set <seed> <paying_account> <hash> <size>" << endl;
	cout << "     account_rate_set <seed> <paying_account> <rate>" << endl;
	cout << "     account_wallet_set <seed> <paying_account> [<wallet_hash>]" << endl;
	cout << "     connect <ip> [<port>]" << endl;
	cout << "     data_delete <key>" << endl;
	cout << "     data_fetch <key>" << endl;
	cout << "     data_store <key> <value>" << endl;
	cout << "     ledger [<id>|current|lastclosed] [full]" << endl;
	cout << "     logrotate " << endl;
	cout << "     nickname_info <nickname>" << endl;
	cout << "     nickname_set <seed> <paying_account> <nickname> [<offer_minimum>] [<authorization>]" << endl;
	cout << "     offer_create <seed> <paying_account> <taker_pays_amount> <taker_pays_currency> <taker_pays_issuer> <takers_gets_amount> <takers_gets_currency> <takers_gets_issuer> <expires> [passive]" << endl;
	cout << "     offer_cancel <seed> <paying_account> <sequence>" << endl;
	cout << "     password_fund <seed> <paying_account> [<account>]" << endl;
	cout << "     password_set <master_seed> <regular_seed> [<account>]" << endl;
	cout << "     peers" << endl;
	cout << "     ripple ..." << endl;
	cout << "     ripple_lines_get <account>|<nickname>|<account_public_key> [<index>]" << endl;
	cout << "     ripple_line_set <seed> <paying_account> <destination_account> <limit_amount> <currency> [<quality_in>] [<quality_out>]" << endl;
	cout << "     send <seed> <paying_account> <account_id> <amount> [<currency>] [<send_max>] [<send_currency>]" << endl;
	cout << "     stop" << endl;
	cout << "     tx <id>" << endl;
	cout << "     unl_add <domain>|<public> [<comment>]" << endl;
	cout << "     unl_delete <domain>|<public_key>" << endl;
	cout << "     unl_list" << endl;
	cout << "     unl_load" << endl;
	cout << "     unl_network" << endl;
	cout << "     unl_reset" << endl;
	cout << "     validation_create [<seed>|<pass_phrase>|<key>]" << endl;
	cout << "     validation_seed [<seed>|<pass_phrase>|<key>]" << endl;
	cout << "     wallet_add <regular_seed> <paying_account> <master_seed> [<initial_funds>] [<account_annotation>]" << endl;
	cout << "     wallet_accounts <seed>" << endl;
	cout << "     wallet_claim <master_seed> <regular_seed> [<source_tag>] [<account_annotation>]" << endl;
	cout << "     wallet_seed [<seed>|<passphrase>|<passkey>]" << endl;
	cout << "     wallet_propose [<passphrase>]" << endl;
}

int main(int argc, char* argv[])
{
	int					iResult	= 0;
	po::variables_map	vm;										// Map of options.
	bool				bTest	= false;

	//
	// Set up option parsing.
	//
	po::options_description desc("Options");
	desc.add_options()
		("help,h", "Display this message.")
		("conf", po::value<std::string>(), "Specify the configuration file.")
		("rpc", "Perform rpc command (default).")
		("standalone,a", "Run with no peers.")
		("test,t", "Perform unit tests.")
		("parameters", po::value< vector<string> >(), "Specify comma separated parameters.")
		("verbose,v", "Increase log level.")
		("load", "Load the current ledger from the local DB.")
		("start", "Start from a fresh Ledger.")
		("net", "Get the initial ledger from the network.")
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
	else if (argc >= 2 && !strcmp(argv[1], "--test")) {
		bTest	= true;
		Log::setMinSeverity(lsTRACE);
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

	if (vm.count("verbose"))
	{
		Log::setMinSeverity(lsTRACE);
	}

	if (!iResult)
	{
		theConfig.setup(vm.count("conf") ? vm["conf"].as<std::string>() : "");

		if (vm.count("standalone"))
		{
			theConfig.RUN_STANDALONE = true;
		}
	}

	if (vm.count("start")) theConfig.START_UP = Config::FRESH;
	else if (vm.count("load")) theConfig.START_UP = Config::LOAD;
	else if (vm.count("net")) theConfig.START_UP = Config::NETWORK;

	if (iResult)
	{
		nothing();
	}
	else if (vm.count("help"))
	{
		iResult	= 1;
	}
	else if (vm.count("test"))
	{
		std::cerr << "--test must be first parameter." << std::endl;
		iResult	= 1;
	}
	else if (bTest)
	{
		iResult	= unit_test_main(init_unit_test, argc, argv);
	}
	else if (!vm.count("parameters"))
	{
		// No arguments. Run server.
		startServer();
	}
	else
	{
		// Have a RPC command.
		std::vector<std::string> vCmd	= vm["parameters"].as<std::vector<std::string> >();

		iResult	= commandLineRPC(vCmd);
	}

	if (1 == iResult)
		printHelp(desc);

	return iResult;
}
// vim:ts=4
