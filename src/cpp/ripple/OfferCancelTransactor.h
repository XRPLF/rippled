//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef OFFERCANCELTRANSACTOR_H
#define OFFERCANCELTRANSACTOR_H

class OfferCancelTransactor : public Transactor
{
public:
    OfferCancelTransactor (const SerializedTransaction& txn, TransactionEngineParams params, TransactionEngine* engine) : Transactor (txn, params, engine) {}

    TER doApply ();
};
#endif

// vim:ts=4
