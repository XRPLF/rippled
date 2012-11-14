
#include "Transactor.h"

class PaymentTransactor : public Transactor
{
	void calculateFee();
public:
	PaymentTransactor(const SerializedTransaction& txn,TransactionEngineParams params, TransactionEngine::pointer engine) : Transactor(txn,params,engine) {}
	
	TER doApply();
};