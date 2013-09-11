//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef TRUSTSETTRANSACTOR_H
#define TRUSTSETTRANSACTOR_H

class TrustSetTransactor : public Transactor
{
public:
    TrustSetTransactor (const SerializedTransaction& txn, TransactionEngineParams params, TransactionEngine* engine) : Transactor (txn, params, engine) {}

    TER doApply ();
};
#endif

// vim:ts=4
