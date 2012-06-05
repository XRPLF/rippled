#ifndef __TRANSACTIONENGINE__
#define __TRANSACTIONENGINE__

#include "Ledger.h"
#include "SerializedTransaction.h"
#include "SerializedLedger.h"

// A TransactionEngine applies serialized transactions to a ledger
// It can also, verify signatures, verify fees, and give rejection reasons

enum TransactionEngineResult
{
	// tenCAN_NEVER_SUCCEED = <0

	// Malformed: Fee claimed
	tenGEN_IN_USE	= -300,	// Generator already in use.
	tenCREATEXNC,			// Can not specify non XNC for Create.
	tenEXPLICITXNC,			// XNC is used by default, don't specify it.
	tenDST_NEEDED,			// Destination not specified.
	tenDST_IS_SRC,			// Destination may not be source.
	tenBAD_GEN_AUTH,		// Not authorized to claim generator.
	tenBAD_ADD_AUTH,		// Not authorized to add account.
	tenBAD_CLAIM_ID,		// Malformed.
	tenBAD_SET_ID,			// Malformed.

	// Invalid: Ledger won't allow.
	tenCLAIMED		= -200,	// Can not claim a previously claimed account.
	tenCREATED,				// Can't add an already created account.
	tenMSG_SET,				// Can't change a message key.

	// Other
	tenFAILED		= -100,	// Something broke horribly
	tenUNKNOWN,				// The transactions requires logic not implemented yet
	tenINSUF_FEE_P,			// fee totally insufficient
	tenINVALID,				// The transaction is ill-formed

	terSUCCESS		= 0,		// The transaction was applied

	// terFAILED_BUT_COULD_SUCCEED = >0
	// Conflict with ledger database: Fee claimed
	// Might succeed if not conflict is not caused by transaction ordering.
	terALREADY,				// The transaction was already in the ledger
	terBAD_SEQ,				// This sequence number should be zero for prepaid transactions.
	terCREATED,				// Can not create a previously created account.
	terDIR_FULL,			// Can not add entry to full dir.
	terINSUF_FEE_B,			// Account balance can't pay fee
	terINSUF_FEE_T,			// fee insufficient now (account doesn't exist, network load)
	terNODE_NOT_FOUND,		// Can not delete a dir node.
	terNODE_NOT_MENTIONED,
	terNODE_NO_ROOT,
	terNO_ACCOUNT,			// The source account does not exist
	terNO_DST,				// The destination does not exist
	terNO_PATH,				// No path existed or met transaction/balance requirements
	terPAST_LEDGER,			// The transaction expired and can't be applied
	terPAST_SEQ,			// This sequence number has already past
	terPRE_SEQ,				// Missing/inapplicable prior transaction
	terUNFUNDED,			// Source account had insufficient balance for transaction.
	terNO_LINE_NO_ZERO,		// Can't zero non-existant line, destination might make it.
	terSET_MISSING_DST,		// Can't set password, destination missing.
	terFUNDS_SPENT,			// Can't set password, password set funds already spent.
	terUNCLAIMED,			// Can not use an unclaimed account.
	terBAD_AUTH,			// Transaction's public key is not authorized.
};

enum TransactionEngineParams
{
	tepNONE          = 0,
	tepNO_CHECK_SIGN = 1,	// Signature already checked
	tepNO_CHECK_FEE  = 2,	// It was voted into a ledger anyway
	tepUPDATE_TOTAL  = 4,	// Update the total coins
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
private:
	TransactionEngineResult dirAdd(
		std::vector<AffectedAccount>&	accounts,
		uint64&							uNodeDir,		// Node of entry.
		const uint256&					uBase,
		const uint256&					uLedgerIndex);

	TransactionEngineResult dirDelete(
		std::vector<AffectedAccount>&	accounts,
		const uint64&					uNodeDir,		// Node item is mentioned in.
		const uint256&					uBase,			// Key of item.
		const uint256&					uLedgerIndex);	// Item being deleted

	TransactionEngineResult	setAuthorized(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts, bool bMustSetGenerator);

protected:
	Ledger::pointer mDefaultLedger, mAlternateLedger;
	Ledger::pointer mLedger;

	TransactionEngineResult doAccountSet(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts);
	TransactionEngineResult doClaim(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts);
	TransactionEngineResult doCreditSet(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts,
								const uint160& uSrcAccountID);
	TransactionEngineResult doDelete(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts);
	TransactionEngineResult doInvoice(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts);
	TransactionEngineResult doOffer(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts);
	TransactionEngineResult doPasswordFund(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts,
								const uint160& uSrcAccountID);
	TransactionEngineResult doPasswordSet(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts);
	TransactionEngineResult doPayment(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts,
								const uint160& uSrcAccountID);
	TransactionEngineResult doStore(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts);
	TransactionEngineResult doTake(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts);
	TransactionEngineResult doTransitSet(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts);
	TransactionEngineResult doWalletAdd(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts);

public:
	TransactionEngine() { ; }
	TransactionEngine(Ledger::pointer ledger) : mDefaultLedger(ledger) { ; }

	Ledger::pointer getDefaultLedger()				{ return mDefaultLedger; }
	void setDefaultLedger(Ledger::pointer ledger)	{ mDefaultLedger = ledger; }
	Ledger::pointer getAlternateLedger()			{ return mAlternateLedger; }
	void setAlternateLedger(Ledger::pointer ledger)	{ mDefaultLedger = ledger; }
	void setLedger(Ledger::pointer ledger)			{ mDefaultLedger = ledger;
													  mAlternateLedger = Ledger::pointer(); }

	TransactionEngineResult applyTransaction(const SerializedTransaction&, TransactionEngineParams,
		uint32 targetLedger);
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
