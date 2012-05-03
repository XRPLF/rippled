
#include "Application.h"
#include "CallRPC.h"
#include "Config.h"
#include "utils.h"

#include <iostream>

#include <boost/program_options.hpp>
namespace po = boost::program_options;

extern bool AddSystemEntropy();
using namespace std;
using namespace boost;

void startServer()
{
	theApp = new Application();
	theApp->run();					// Blocks till we get a stop RPC.
}

void printHelp(const po::options_description& desc)
{
	cout << "newcoin [options] <command> <params>" << endl;

	cout << desc << endl;

	cout << "Commands: " << endl;
	cout << "     accountinfo <family>:<key>" << endl;
	cout << "     connect <ip> [<port>]" << endl;
	cout << "     createfamily [<key>]" << endl;
	cout << "     familyinfo" << endl;
	cout << "     ledger" << endl;
	cout << "     lock <family>" << endl;
	cout << "     newaccount <family> [<name>]" << endl;
	cout << "     peers" << endl;
	cout << "     sendto <destination> <amount> [<tag>]" << endl;
	cout << "     stop" << endl;
	cout << "     tx" << endl;
	cout << "     unl_add <domain>|<public> [<comment>]" << endl;
	cout << "     unl_delete <public_key>" << endl;
	cout << "     unl_list" << endl;
	cout << "     unl_reset" << endl;
	cout << "     unlock <passphrase>" << endl;
	cout << "     validation_create [<seed>|<pass_phrase>|<key>]" << endl;
}

int main(int argc, char* argv[])
{
	int					iResult	= 0;
	po::variables_map	vm;										// Map of options.

	//
	// Set up option parsing.
	//
	po::options_description desc("Options");
	desc.add_options()
		("help,h", "Display this message.")
		("command", po::value< vector<string> >(), "Specify a comma seperated RPC command.")
	;

	po::positional_options_description p;
	p.add("command", -1);

	//
	// Prepare to run
	//
	theConfig.load();

	if (!AddSystemEntropy())
	{
		std::cerr << "Unable to add system entropy" << std::endl;
		iResult	= 2;
	}

	// Parse options, if no error.
	if (!iResult)
	{
		po::store(po::command_line_parser(argc, argv)
			.options(desc)											// Parse options.
			.positional(p)											// Remainder as --command.
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
	else if (!vm.count("command"))
	{
		// No arguments. Run server.
		startServer();
	}
	else
	{
		// Have a RPC command.
		std::vector<std::string> vCmd	= vm["command"].as<std::vector<std::string> >();

		iResult	= commandLineRPC(vCmd);
	}

	if (1 == iResult)
		printHelp(desc);

	return iResult;
}
// vim:ts=4
