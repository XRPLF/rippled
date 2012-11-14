#include "Transactor.h"


class WalletAddTransactor : public Transactor
{
public:
	WalletAddTransactor(const SerializedTransaction& txn,TransactionEngineParams params, TransactionEngine::pointer engine) : Transactor(txn,params,engine) {}

	TER doApply();
};