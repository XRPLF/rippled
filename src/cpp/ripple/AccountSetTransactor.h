#include "Transactor.h"

class AccountSetTransactor : public Transactor
{
public:
	AccountSetTransactor(const SerializedTransaction& txn,TransactionEngineParams params, TransactionEngine::pointer engine) : Transactor(txn,params,engine) {}

	TER doApply();
};