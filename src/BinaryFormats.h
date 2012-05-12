#ifndef __BINARYFORMATS__
#define __BINARYFORMATS__

// ledger (note: fields after the timestamp are not part of the hash)
const int BLgSize=190;
const int BLgPIndex=0, BLgLIndex=4;			// ledger index
const int BLgPTotCoins=4, BLgLTotCoins=8;	// total native coins
const int BLgPPrevLg=12, BLgLPrevLg=32;		// previous ledger hash
const int BLgPTxT=44, BLgLTxT=32;			// transaction tree hash
const int BLgPAcT=76, BLgLPAct=32;			// account state hash
const int BLgPClTs=108, BLgLClTs=8;			// closing timestamp
const int BLgPNlIn=116, BLgLNlIn=2;
const int BLgPSig=118, BLgLSig=72;			// signature

#endif
