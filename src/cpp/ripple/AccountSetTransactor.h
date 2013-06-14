#ifndef ACCOUNTSETTRANSACTOR_H
#define ACCOUNTSETTRANSACTOR_H

class AccountSetTransactor : public Transactor
{
public:
    AccountSetTransactor (const SerializedTransaction& txn, TransactionEngineParams params, TransactionEngine* engine) : Transactor (txn, params, engine) {}

    TER doApply ();
};
#endif

// vim:ts=4
