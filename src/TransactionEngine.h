#ifndef __TRANSACTIONENGINE__
#define __TRANSACTIONENGINE__

#include "Ledger.h"
#include "SerializedTransaction.h"

// A TransactionEngine applies serialized transactions to a ledger
// It can also, verify signatures, verify fees, and give rejection reasons

enum TransactionEngineResult
{ // <0 = Can never succeed, 0 = success, >0 = failed, but could succeed
	terUNKNOWN     = -3,	// The transactions requires logic not implemented yet
	terINSUF_FEE_P = -2,	// fee totally insufficient
	terINVALID     = -1,	// The transaction is ill-formed
	terSUCCESS     = 0,		// The transaction was applied
	terALREADY,				// The transaction was already in the ledger
	terINSUF_FEE_T,			// fee insufficient now (account doesn't exist, network load)
	terUNFUNDED,			// Source account had insufficient balance
	terNO_PATH,				// No path existed or met transaction/balance requirements
	terPAST_SEQ,			// This sequence number has already past
	terPRE_SEQ,				// Missing/inapplicable prior transaction
	terPAST_LEDGER,			// The transaction expired and can't be applied
};

enum TransactionEngineParams
{
	tepNO_CHECK_SIGN = 1,	// Signature already checked
	tepNO_CHECK_FEE  = 2,	// It was voted into a ledger anyway
};

class TransactionEngine
{
protected:
	Ledger::pointer mTargetLedger;

	TransactionEngineResult doPayment(const SerializedTransaction&);
	TransactionEngineResult doInvoice(const SerializedTransaction&);
	TransactionEngineResult doOffer(const SerializedTransaction&);
	TransactionEngineResult doTake(const SerializedTransaction&);
	TransactionEngineResult doCancel(const SerializedTransaction&);
	TransactionEngineResult doStore(const SerializedTransaction&);
	TransactionEngineResult doDelete(const SerializedTransaction&);

public:
	TransactionEngine(Ledger::pointer targetLedger) : mTargetLedger(targetLedger) { ; }

	Ledger::pointer getTargetLedger() { return mTargetLedger; }
	void setTargetLedger(Ledger::pointer targetLedger) { mTargetLedger = targetLedger; }

	TransactionEngineResult applyTransaction(const SerializedTransaction&, TransactionEngineParams);
};

#endif
