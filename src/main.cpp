
#include "Application.h"
#include "CallRPC.h"
#include "Config.h"
#include "utils.h"

#include <iostream>

#include <boost/foreach.hpp>
#include <boost/program_options.hpp>
#include <boost/test/included/unit_test.hpp>

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
	nothing();

    return true;
}

void printHelp(const po::options_description& desc)
{
	cout << "newcoin [options] <command> <params>" << endl;

	cout << desc << endl;

	cout << "Commands: " << endl;
	cout << "     account_info <account>|<nickname>" << endl;
	cout << "     account_info <seed>|<pass_phrase>|<key> [<index>]" << endl;
	cout << "     connect <ip> [<port>]" << endl;
	cout << "     credit_set <seed> <paying_account> <destination_account> <limit_amount> <currency> [<account_rate>]" << endl;
	cout << "     ledger" << endl;
	cout << "     peers" << endl;
	cout << "     send <seed> <paying_account> <account_id> <amount> [<currency>] [<send_max>] [<send_currency>]" << endl;
	cout << "     stop" << endl;
	cout << "     transit_set <seed> <paying_account> <transit_rate> <starts> <expires>" << endl;
	cout << "     tx" << endl;
	cout << "     unl_add <domain>|<public> [<comment>]" << endl;
	cout << "     unl_delete <public_key>" << endl;
	cout << "     unl_list" << endl;
	cout << "     unl_reset" << endl;
	cout << "     validation_create [<seed>|<pass_phrase>|<key>]" << endl;
	cout << "     wallet_claim <master_seed> <regular_seed> [<source_tag>] [<account_annotation>]" << endl;
	cout << "     wallet_seed [<seed>|<passphrase>|<passkey>]" << endl;
	cout << "     wallet_propose" << endl;
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
		("rpc", "Perform rpc command (default).")
		("test,t", "Perform unit tests.")
		("parameters", po::value< vector<string> >(), "Specify comma separated parameters.")
	;

	// Interpret positional arguments as --parameters.
	po::positional_options_description p;
	p.add("parameters", -1);

	//
	// Prepare to run
	//
	theConfig.load();

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
	}
	else
	{
		// Parse options, if no error.
		po::store(po::command_line_parser(argc, argv)
			.options(desc)											// Parse options.
			.positional(p)											// Remainder as --parameters.
			.run(),
			vm);
		po::notify(vm);												// Invoke option notify functions.
	}

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
