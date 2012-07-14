#ifndef __TRANSACTIONENGINE__
#define __TRANSACTIONENGINE__

#include "Ledger.h"
#include "SerializedTransaction.h"
#include "SerializedLedger.h"

// A TransactionEngine applies serialized transactions to a ledger
// It can also, verify signatures, verify fees, and give rejection reasons

enum TransactionEngineResult
{
	// Note: Numbers are currently unstable.  Use tokens.

	// tenCAN_NEVER_SUCCEED = <0

	// Malformed: Fee claimed
	tenGEN_IN_USE	= -300,
	tenBAD_ADD_AUTH,
	tenBAD_AMOUNT,
	tenBAD_CLAIM_ID,
	tenBAD_EXPIRATION,
	tenBAD_GEN_AUTH,
	tenBAD_ISSUER,
	tenBAD_OFFER,
	tenBAD_SET_ID,
	tenCREATEXNS,
	tenDST_IS_SRC,
	tenDST_NEEDED,
	tenEXPLICITXNS,
	tenREDUNDANT,
	tenRIPPLE_EMPTY,

	// Invalid: Ledger won't allow.
	tenCLAIMED		= -200,
	tenBAD_RIPPLE,
	tenCREATED,
	tenEXPIRED,
	tenMSG_SET,
	terALREADY,

	// Other
	tenFAILED		= -100,
	tenINSUF_FEE_P,
	tenINVALID,
	tenUNKNOWN,

	terSUCCESS		= 0,

	// terFAILED_BUT_COULD_SUCCEED = >0
	// Conflict with ledger database: Fee claimed
	// Might succeed if not conflict is not caused by transaction ordering.
	terBAD_AUTH,
	terBAD_AUTH_MASTER,
	terBAD_LEDGER,
	terBAD_RIPPLE,
	terBAD_SEQ,
	terCREATED,
	terDIR_FULL,
	terFUNDS_SPENT,
	terINSUF_FEE_B,
	terINSUF_FEE_T,
	terNODE_NOT_FOUND,
	terNODE_NOT_MENTIONED,
	terNODE_NO_ROOT,
	terNO_ACCOUNT,
	terNO_DST,
	terNO_LINE_NO_ZERO,
	terNO_PATH,
	terOFFER_NOT_FOUND,
	terOVER_LIMIT,
	terPAST_LEDGER,
	terPAST_SEQ,
	terPRE_SEQ,
	terSET_MISSING_DST,
	terUNCLAIMED,
	terUNFUNDED,
};

bool transResultInfo(TransactionEngineResult terCode, std::string& strToken, std::string& strHuman);

enum TransactionEngineParams
{
	tepNONE          = 0,
	tepNO_CHECK_SIGN = 1,	// Signature already checked
	tepNO_CHECK_FEE  = 2,	// It was voted into a ledger anyway
	tepUPDATE_TOTAL  = 4,	// Update the total coins
};

enum TransactionAccountAction
{
	taaNONE,
	taaCREATE,
	taaMODIFY,
	taaDELETE,
	taaUNFUNDED,
};

typedef std::pair<TransactionAccountAction, SerializedLedgerEntry::pointer> AffectedAccount;

// One instance per ledger.
// Only one transaction applied at a time.
class TransactionEngine
{
private:
	typedef boost::unordered_map<SLE::pointer, TransactionAccountAction>					entryMap;
	typedef boost::unordered_map<SLE::pointer, TransactionAccountAction>::iterator			entryMap_iterator;
	typedef boost::unordered_map<SLE::pointer, TransactionAccountAction>::const_iterator	entryMap_const_iterator;
	typedef boost::unordered_map<SLE::pointer, TransactionAccountAction>::iterator::value_type	entryMap_value_type;

	TransactionEngineResult dirAdd(
		uint64&							uNodeDir,		// Node of entry.
		const uint256&					uRootIndex,
		const uint256&					uLedgerIndex);

	TransactionEngineResult dirDelete(
		bool							bKeepRoot,
		const uint64&					uNodeDir,		// Node item is mentioned in.
		const uint256&					uRootIndex,
		const uint256&					uLedgerIndex);	// Item being deleted

#ifdef WORK_IN_PROGRESS
	typedef struct {
		STAmount						saWanted;		// What this node wants from upstream.

		STAmount						saIOURedeem;	// What this node will redeem downstream.
		STAmount						saIOUIssue;		// What this node will issue downstream.
		STAmount						saSend;			// Amount of stamps this node will send.

		STAmount						saIOUForgive;	// Amount of IOUs to forgive.
		STAmount						saIOUAccept;	// Amount of IOUs to accept.
		STAmount						saRecieve;		// Amount stamps to receive.

		STAccount						saAccount;
	} paymentNode;

	typedef struct {
		boost::unordered_set<....>	offersDeletedAlways;
		boost::unordered_set<....>	offersDeletedOnSuccess;
		std::vector<paymentNode>	vpnNodes;
		bool						bAllowPartial;
	} paymentGroup;
#endif

	TransactionEngineResult	setAuthorized(const SerializedTransaction& txn, SLE::pointer sleSrc, bool bMustSetGenerator);

	TransactionEngineResult takeOffers(
		bool			bPassive,
		const uint256&	uBookBase,
		const uint160&	uTakerAccountID,
		const STAmount&	saTakerPays,
		const STAmount&	saTakerGets,
		const STAmount&	saTakerFunds,
		STAmount&		saTakerPaid,
		STAmount&		saTakerGot);

protected:
	Ledger::pointer mDefaultLedger, mAlternateLedger;
	Ledger::pointer mLedger;
	uint64			mLedgerParentCloseTime;

	entryMap		mEntries;

	SLE::pointer	entryCreate(LedgerEntryType letType, const uint256& uIndex);
	void			entryDelete(SLE::pointer sleEntry);
	void			entryModify(SLE::pointer sleEntry);
	void			entryUnfunded(SLE::pointer sleEntry);
	bool			entryExists(SLE::pointer sleEntry);

	STAmount	rippleBalance(const uint160& uAccountID, const uint160& uIssuerAccountID, const uint160& uCurrency);

	void		rippleCredit(const uint160& uAccountID, const uint160& uIssuerAccountID, const uint160& uCurrency, const STAmount& saCredit);
	void		rippleDebit(const uint160& uAccountID, const uint160& uIssuerAccountID, const uint160& uCurrency, const STAmount& saDebit);

	TransactionEngineResult doAccountSet(const SerializedTransaction& txn, SLE::pointer sleSrc);
	TransactionEngineResult doClaim(const SerializedTransaction& txn, SLE::pointer sleSrc);
	TransactionEngineResult doCreditSet(const SerializedTransaction& txn, const uint160& uSrcAccountID);
	TransactionEngineResult doDelete(const SerializedTransaction& txn);
	TransactionEngineResult doInvoice(const SerializedTransaction& txn);
	TransactionEngineResult doOfferCreate(const SerializedTransaction& txn, SLE::pointer sleSrc, const uint160& uSrcAccountID);
	TransactionEngineResult doOfferCancel(const SerializedTransaction& txn, const uint160& uSrcAccountID);
	TransactionEngineResult doNicknameSet(const SerializedTransaction& txn, SLE::pointer sleSrc, const uint160& uSrcAccountID);
	TransactionEngineResult doPasswordFund(const SerializedTransaction& txn, SLE::pointer sleSrc, const uint160& uSrcAccountID);
	TransactionEngineResult doPasswordSet(const SerializedTransaction& txn, SLE::pointer sleSrc);
	TransactionEngineResult doPayment(const SerializedTransaction& txn, SLE::pointer sleSrc, const uint160& uSrcAccountID);
	TransactionEngineResult doStore(const SerializedTransaction& txn);
	TransactionEngineResult doTake(const SerializedTransaction& txn);
	TransactionEngineResult doWalletAdd(const SerializedTransaction& txn, SLE::pointer sleSrc);

public:
	TransactionEngine() { ; }
	TransactionEngine(Ledger::pointer ledger) : mDefaultLedger(ledger) { ; }

	Ledger::pointer getDefaultLedger()				{ return mDefaultLedger; }
	void setDefaultLedger(Ledger::pointer ledger)	{ mDefaultLedger = ledger; }
	Ledger::pointer getAlternateLedger()			{ return mAlternateLedger; }
	void setAlternateLedger(Ledger::pointer ledger)	{ mAlternateLedger = ledger; }
	void setLedger(Ledger::pointer ledger)			{ mDefaultLedger = ledger;
													  mAlternateLedger = Ledger::pointer(); }

	Ledger::pointer getTransactionLedger(uint32 targetLedger);
	TransactionEngineResult applyTransaction(const SerializedTransaction&, TransactionEngineParams,
		Ledger::pointer ledger);
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
