#ifndef TRUSTSETTRANSACTOR_H
#define TRUSTSETTRANSACTOR_H

#include "Transactor.h"

class TrustSetTransactor : public Transactor
{
public:
    TrustSetTransactor (const SerializedTransaction& txn, TransactionEngineParams params, TransactionEngine* engine) : Transactor (txn, params, engine) {}

    TER doApply ();
};
#endif

// vim:ts=4
