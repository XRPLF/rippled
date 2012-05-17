#ifndef __TRANSACTIONENGINE__
#define __TRANSACTIONENGINE__

#include "Ledger.h"
#include "Ledger.h"
#include "Currency.h"
#include "SerializedTransaction.h"
#include "SerializedLedger.h"

// A TransactionEngine applies serialized transactions to a ledger
// It can also, verify signatures, verify fees, and give rejection reasons

enum TransactionEngineResult
{
	// tenCAN_NEVER_SUCCEED = <0

	// Malformed
	tenGEN_IN_USE  = -300,	// Generator already in use.
	tenCREATEXNC,			// Can not specify non XNC for Create.

	// Not possible due to ledger database.
	tenCREATED		= -200,	// Can not create a previously created account.
	tenCLAIMED,				// Can not claim a previously claimed account.

	// Other
	tenFAILED		= -100,	// Something broke horribly
	tenUNKNOWN,				// The transactions requires logic not implemented yet
	tenINSUF_FEE_P,			// fee totally insufficient
	tenINVALID,				// The transaction is ill-formed

	terSUCCESS     = 0,		// The transaction was applied

	// terFAILED_BUT_COULD_SUCEED = >0
	terALREADY,				// The transaction was already in the ledger
	terNO_ACCOUNT,			// The source account does not exist
	terNO_TARGET,			// The destination does not exist
	terINSUF_FEE_T,			// fee insufficient now (account doesn't exist, network load)
	terINSUF_FEE_B,			// Account balance can't pay fee
	terUNFUNDED,			// Source account had insufficient balance for transactin
	terNO_PATH,				// No path existed or met transaction/balance requirements
	terPAST_SEQ,			// This sequence number has already past
	terBAD_SEQ,				// This sequence number should be zero for prepaid transactions.
	terPRE_SEQ,				// Missing/inapplicable prior transaction
	terPAST_LEDGER,			// The transaction expired and can't be applied
};

enum TransactionEngineParams
{
	tepNONE          = 0,
	tepNO_CHECK_SIGN = 1,	// Signature already checked
	tepNO_CHECK_FEE  = 2,	// It was voted into a ledger anyway
};

enum TransactionAccountAction
{
	taaACCESS,
	taaCREATE,
	taaMODIFY,
	taaDELETE
};

typedef std::pair<TransactionAccountAction, SerializedLedgerEntry::pointer> AffectedAccount;

class TransactionEngine
{
protected:
	Ledger::pointer mLedger;

	TransactionEngineResult doCancel(const SerializedTransaction&, std::vector<AffectedAccount>&);
	TransactionEngineResult doClaim(const SerializedTransaction&, std::vector<AffectedAccount>&);
	TransactionEngineResult doDelete(const SerializedTransaction&, std::vector<AffectedAccount>&);
	TransactionEngineResult doInvoice(const SerializedTransaction&, std::vector<AffectedAccount>&);
	TransactionEngineResult doOffer(const SerializedTransaction&, std::vector<AffectedAccount>&);
	TransactionEngineResult doPayment(const SerializedTransaction&, std::vector<AffectedAccount>&,
								uint160 srcAccountID);
	TransactionEngineResult doStore(const SerializedTransaction&, std::vector<AffectedAccount>&);
	TransactionEngineResult doTake(const SerializedTransaction&, std::vector<AffectedAccount>&);

public:
	TransactionEngine() { ; }
	TransactionEngine(Ledger::pointer ledger) : mLedger(ledger) { ; }

	Ledger::pointer getLedger() { return mLedger; }
	void setLedger(Ledger::pointer ledger) { mLedger = ledger; }

	TransactionEngineResult applyTransaction(const SerializedTransaction&, TransactionEngineParams);
};

inline TransactionEngineParams operator|(const TransactionEngineParams& l1, const TransactionEngineParams& l2)
{
	return static_cast<TransactionEngineParams>(static_cast<int>(l1) | static_cast<int>(l2));
}

inline TransactionEngineParams operator&(const TransactionEngineParams& l1, const TransactionEngineParams& l2)
{
	return static_cast<TransactionEngineParams>(static_cast<int>(l1) & static_cast<int>(l2));
}

#endif
// vim:ts=4
