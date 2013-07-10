//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_DBINIT_H_INCLUDED
#define RIPPLE_DBINIT_H_INCLUDED

// VFALCO TODO Tidy these up into a class with functions and return types.
extern const char* RpcDBInit[];
extern const char* TxnDBInit[];
extern const char* LedgerDBInit[];
extern const char* WalletDBInit[];
extern const char* HashNodeDBInit[];

// VFALCO TODO Figure out what these counts are for
extern int RpcDBCount;
extern int TxnDBCount;
extern int LedgerDBCount;
extern int WalletDBCount;
extern int HashNodeDBCount;

// VFALCO TODO Seems these two aren't used so delete EVERYTHING.
extern const char* NetNodeDBInit[];
extern const char* PathFindDBInit[];
extern int NetNodeDBCount;
extern int PathFindDBCount;

#endif
