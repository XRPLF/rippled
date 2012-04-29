#include "Application.h"
#include <iostream>
#include "CallRPC.h"
#include "Config.h"

extern void runTests();
using namespace std;
using namespace boost;

/*
	Detect if another is running
	If so message it with the users command
*/


void startApp()
{
	theApp=new Application();
	theApp->run(); // blocks till we get a stop RPC
}

void printHelp()
{
	cout << "newcoin [options] <command> <params>" << endl;
	cout << "options: " << endl;
	cout << "     -" << endl;
	cout << "commands: " << endl;
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

int parseCommandline(int argc, char* argv[])
{
	int ret=0;

	theConfig.load();

	if(argc>1)
	{
		ret=commandLineRPC(argc, argv);
		if(ret)
			printHelp();
	}
	else startApp();

	return ret;
}


int main(int argc, char* argv[])
{
//	runTests();

	return(parseCommandline(argc,argv));
}
// vim:ts=4
