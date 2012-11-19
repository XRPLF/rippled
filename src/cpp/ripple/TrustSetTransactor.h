#include "Transactor.h"

class TrustSetTransactor : public Transactor
{
public:
	TrustSetTransactor(const SerializedTransaction& txn,TransactionEngineParams params, TransactionEngine* engine) : Transactor(txn,params,engine) {}
	
	TER doApply();
};