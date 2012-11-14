#include "Transactor.h"

class RegularKeySetTransactor : public Transactor
{
	void calculateFee();
public:
	RegularKeySetTransactor(const SerializedTransaction& txn,TransactionEngineParams params, TransactionEngine::pointer engine) : Transactor(txn,params,engine) {}
	TER checkFee();
	TER checkSig();
	TER doApply();
};
