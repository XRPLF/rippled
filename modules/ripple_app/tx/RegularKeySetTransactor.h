//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef REGULARKEYSETTRANSACTOR_H
#define REGULARKEYSETTRANSACTOR_H

class RegularKeySetTransactor : public Transactor
{
    uint64 calculateBaseFee ();
public:
    RegularKeySetTransactor (const SerializedTransaction& txn, TransactionEngineParams params, TransactionEngine* engine) : Transactor (txn, params, engine) {}
    TER checkFee ();
    TER doApply ();
};
#endif

// vim:ts=4
