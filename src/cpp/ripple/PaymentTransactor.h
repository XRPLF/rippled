#ifndef __PAYMENTTRANSACTOR__
#define __PAYMENTTRANSACTOR__

#include "Transactor.h"

class PaymentTransactor : public Transactor
{
public:
	PaymentTransactor(const SerializedTransaction& txn,TransactionEngineParams params, TransactionEngine* engine) : Transactor(txn,params,engine) {}

	TER doApply();
};
#endif

// vim:ts=4
