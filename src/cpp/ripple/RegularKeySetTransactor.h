#include "Transactor.h"

class RegularKeySetTransactor : public Transactor
{
	uint64_t calculateBaseFee();
public:
	RegularKeySetTransactor(const SerializedTransaction& txn,TransactionEngineParams params, TransactionEngine* engine) : Transactor(txn,params,engine) {}
	TER checkFee();
	TER doApply();
};
