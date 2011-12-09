#ifndef __NETWORKSTATUS__
#define __NETWORKSTATUS__

struct NSBit
{ // a network status bit
 const char *name, *description;
 int number;
};

struct NetworkStatus
{
 static const int nsbConnected=0;		// connected to the network
 static const int nsbAccepted=1;		// accept this as the real network
 static const int nsbFastSynching=2;	// catching up, skipping transactions
 static const int nsbSlowSynching=3;	// catching up, txn by txn
 static const int nsbSynched=4;			// in synch with the network
 static const int nsbAnonymous=5;		// hiding our identity
 static const int nsbLedgerSync=6;		// participating in ledger sync
 static const int nsbStuck=7;			// unable to sync

 static const int nnbCount=32;
 std::bitset<nnbCount> nsbValues;
 std::map<int,NSBit> nsbData;
};

#endif
