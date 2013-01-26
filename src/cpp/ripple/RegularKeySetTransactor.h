#include "Transactor.h"

class RegularKeySetTransactor : public Transactor
{
	uint64 calculateBaseFee();
public:
	RegularKeySetTransactor(const SerializedTransaction& txn,TransactionEngineParams params, TransactionEngine* engine) : Transactor(txn,params,engine) {}
	TER checkFee();
	TER doApply();
};

// vim:ts=4
