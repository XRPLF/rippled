#include "Application.h"
#include <iostream>
#include "CallRPC.h"

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
	cout << "     stop" << endl;
	cout << "     send <address> <amount>" << endl;
	cout << "     getinfo" << endl;
	cout << "     getbalance" << endl;

}

int parseCommandline(int argc, char* argv[])
{
	int ret=0;
	if(argc>1)
	{
		ret=commandLineRPC(argc, argv);
		if(!ret) printHelp();
	}else startApp();
	return(ret);
}


int main(int argc, char* argv[])
{
	return(parseCommandline(argc,argv));
}

