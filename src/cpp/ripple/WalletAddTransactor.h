#ifndef WALLETADDTRANSACTOR_H
#define WALLETADDTRANSACTOR_H

class WalletAddTransactor : public Transactor
{
public:
    WalletAddTransactor (const SerializedTransaction& txn, TransactionEngineParams params, TransactionEngine* engine) : Transactor (txn, params, engine) {}

    TER doApply ();
};
#endif

// vim:ts=4
