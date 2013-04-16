#include "Transactor.h"

class ChangeTransactor : public Transactor
{
protected:

	TER applyFeature();
	TER applyFee();

public:
	ChangeTransactor(const SerializedTransaction& txn, TransactionEngineParams params, TransactionEngine *engine)
		: Transactor(txn, params, engine)
	{ ; }

	TER doApply();
	TER checkSig();
	TER checkSeq();
	TER payFee();
};

// vim:ts=4
