#include "Transactor.h"

class OfferCancelTransactor : public Transactor
{
public:
	OfferCancelTransactor(const SerializedTransaction& txn,TransactionEngineParams params, TransactionEngine* engine) : Transactor(txn,params,engine) {}
	
	TER doApply();
};