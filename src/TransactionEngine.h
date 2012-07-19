#ifndef __TRANSACTIONENGINE__
#define __TRANSACTIONENGINE__

#include <boost/unordered_set.hpp>
#include <boost/unordered_map.hpp>

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
	taaCACHED,				// Unmodified.
	taaMODIFY,				// Modifed, must have previously been taaCACHED.
	taaDELETE,				// Delete, must have previously been taaDELETE or taaMODIFY.
	taaCREATE,				// Newly created.
};

typedef std::pair<TransactionAccountAction, SerializedLedgerEntry::pointer> AffectedAccount;

// One instance per ledger.
// Only one transaction applied at a time.
class TransactionEngine
{
private:
	typedef boost::unordered_map<uint256, std::pair<SLE::pointer, TransactionAccountAction> >		entryMap;
	typedef entryMap::iterator				entryMap_iterator;
	typedef entryMap::const_iterator		entryMap_const_iterator;
	typedef entryMap::iterator::value_type	entryMap_value_type;

	TransactionEngineResult dirAdd(
		uint64&							uNodeDir,		// Node of entry.
		const uint256&					uRootIndex,
		const uint256&					uLedgerIndex);

	TransactionEngineResult dirDelete(
		bool							bKeepRoot,
		const uint64&					uNodeDir,		// Node item is mentioned in.
		const uint256&					uRootIndex,
		const uint256&					uLedgerIndex);	// Item being deleted

	void dirFirst(const uint256& uRootIndex, uint256& uEntryIndex, uint64& uEntryNode);

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
		std::vector<paymentNode>	vpnNodes;
		bool						bAllowPartial;
	} paymentGroup;
#endif

	TransactionEngineResult	setAuthorized(const SerializedTransaction& txn, bool bMustSetGenerator);

	TransactionEngineResult takeOffers(
		bool				bPassive,
		const uint256&		uBookBase,
		const uint160&		uTakerAccountID,
		const SLE::pointer&	sleTakerAccount,
		const STAmount&		saTakerPays,
		const STAmount&		saTakerGets,
		STAmount&			saTakerPaid,
		STAmount&			saTakerGot);

protected:
	Ledger::pointer mLedger;
	uint64			mLedgerParentCloseTime;

	uint160			mTxnAccountID;
	SLE::pointer	mTxnAccount;

	entryMap		mEntries;
	boost::unordered_set<uint256>	mUnfunded;	// Indexes that were found unfunded.

	SLE::pointer	entryCreate(LedgerEntryType letType, const uint256& uIndex);
	SLE::pointer	entryCache(LedgerEntryType letType, const uint256& uIndex);
	void			entryDelete(SLE::pointer sleEntry);
	void			entryModify(SLE::pointer sleEntry);

	void			entryReset(const SerializedTransaction& txn);

	STAmount		rippleHolds(const uint160& uAccountID, const uint160& uCurrency, const uint160& uIssuerID);
	STAmount		rippleTransit(const uint160& uSenderID, const uint160& uReceiverID, const uint160& uIssuerID, const STAmount& saAmount);
	STAmount		rippleSend(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount);

	STAmount		accountHolds(const uint160& uAccountID, const uint160& uCurrency, const uint160& uIssuerID);
	STAmount		accountSend(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount);
	STAmount		accountFunds(const uint160& uAccountID, const STAmount& saDefault);

	void			txnWrite();

	TransactionEngineResult offerDelete(const SLE::pointer& sleOffer, const uint256& uOfferIndex, const uint160& uOwnerID);

	TransactionEngineResult doAccountSet(const SerializedTransaction& txn);
	TransactionEngineResult doClaim(const SerializedTransaction& txn);
	TransactionEngineResult doCreditSet(const SerializedTransaction& txn);
	TransactionEngineResult doDelete(const SerializedTransaction& txn);
	TransactionEngineResult doInvoice(const SerializedTransaction& txn);
	TransactionEngineResult doOfferCreate(const SerializedTransaction& txn);
	TransactionEngineResult doOfferCancel(const SerializedTransaction& txn);
	TransactionEngineResult doNicknameSet(const SerializedTransaction& txn);
	TransactionEngineResult doPasswordFund(const SerializedTransaction& txn);
	TransactionEngineResult doPasswordSet(const SerializedTransaction& txn);
	TransactionEngineResult doPayment(const SerializedTransaction& txn);
	TransactionEngineResult doStore(const SerializedTransaction& txn);
	TransactionEngineResult doTake(const SerializedTransaction& txn);
	TransactionEngineResult doWalletAdd(const SerializedTransaction& txn);

public:
	TransactionEngine() { ; }
	TransactionEngine(Ledger::pointer ledger) : mLedger(ledger) { ; }

	Ledger::pointer getLedger()						{ return mLedger; }
	void setLedger(Ledger::pointer ledger)			{ assert(ledger); mLedger = ledger; }

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
