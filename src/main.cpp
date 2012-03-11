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
	cout << "     createfamily [<key>]" << endl;
	cout << "     accountinfo <family>:<key>" << endl;
	cout << "     newaccount <family> [<name>]" << endl;
	cout << "     lock <family>" << endl;
	cout << "     unlock <passphrase>" << endl;
	cout << "     familyinfo" << endl;
	cout << "     connect <ip> [<port>]" << endl;
	cout << "     peers" << endl;
	cout << "     sendto <destination> <amount> [<tag>]" << endl;
	cout << "     tx" << endl;
	cout << "     ledger" << endl;
	cout << "     stop" << endl;

}

int parseCommandline(int argc, char* argv[])
{
	int ret=0;
	if(argc>1)
	{
		theConfig.load();
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

