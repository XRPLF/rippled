#ifndef REGULARKEYSETTRANSACTOR_H
#define REGULARKEYSETTRANSACTOR_H

#include "Transactor.h"

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
