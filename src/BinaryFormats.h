#ifndef __BINARYFORMATS__
#define __BINARYFORMATS__

// binary transaction
const int BTxSize=145;
const int BTxPDestAcct=0, BTxLDestAact=20;	// destination account pubkey Hash160
const int BTxPAmount=20, BTxLAmount=8;		// amount
const int BTxPSASeq=28, BTxLASeq=4;			// source account sequence number
const int BTxPSLIdx=32, BTxLSLIdx=4;		// source ledger index
const int BTxPSTag=36, BTxLSTag=4;			// source tag
const int BTxPSPubK=40, BTxLSPubK=33;		// source public key
const int BTxPSig=73, BTxLSig=72;			// signature

// ledger (note: fields after the timestamp are not part of the hash)
const int BLgSize=192;
const int BLgPIndex=0, BLgLIndex=4;			// ledger index
const int BLgPFeeHeld=4, BLgLFeeHeld=8;		// transaction fees held
const int BLgPPrevLg=12, BLgLPrevLg=32;		// previous ledger hash
const int BLgPTxT=44, BLgLTxT=32;			// transaction tree hash
const int BLgPAcT=76, BLgLPAct=32;			// account state hash
const int BLgPClTs=108, BLgLClTs=8;			// closing timestamp
const int BLgPConf=116, BLgLPConf=4;		// confidence
const int BLgPSig=120, BLgLSig=72;			// signature

// account status
const int BAsSize=32;
const int BAsPID=0, BAsLID=20;				// account pubkey Hash160
const int BAsPBalance=20, BAsLBalance=8;	// account balance
const int BAsPSequence=28, BASLSequence=4;	// account sequence

#endif
