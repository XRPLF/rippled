#ifndef __TRANSACTIONENGINE__
#define __TRANSACTIONENGINE__

#include <boost/unordered_set.hpp>
#include <boost/unordered_map.hpp>

#include "Ledger.h"
#include "SerializedTransaction.h"
#include "SerializedLedger.h"
#include "LedgerEntrySet.h"

// A TransactionEngine applies serialized transactions to a ledger
// It can also, verify signatures, verify fees, and give rejection reasons

enum TransactionEngineResult
{
	// Note: Range is stable.  Exact numbers are currently unstable.  Use tokens.

	// -399 .. -300: L Local error (transaction fee inadequate, exceeds local limit)
	// Not forwarded, no fee. Only valid during non-consensus processing
	telLOCAL_ERROR	= -399,
	telBAD_PATH_COUNT,
	telINSUF_FEE_P,

	// -299 .. -200: M Malformed (bad signature)
	// Transaction corrupt, not forwarded, cannot charge fee, reject
	// Never can succeed in any ledger
	temMALFORMED	= -299,
	temBAD_AMOUNT,
	temBAD_AUTH_MASTER,
	temBAD_EXPIRATION,
	temBAD_ISSUER,
	temBAD_OFFER,
	temBAD_PUBLISH,
	temBAD_SET_ID,
	temCREATEXNS,
	temDST_IS_SRC,
	temDST_NEEDED,
	temINSUF_FEE_P,
	temINVALID,
	temREDUNDANT,
	temRIPPLE_EMPTY,
	temUNCERTAIN,
	temUNKNOWN,

	// -199 .. -100: F Failure (sequence number previously used)
	// Transaction cannot succeed because of ledger state, unexpected ledger state, C++ exception, not forwarded, cannot be
	// applied, Could succeed in an imaginary ledger.
	tefFAILURE		= -199,
	tefALREADY,
	tefBAD_ADD_AUTH,
	tefBAD_AUTH,
	tefBAD_CLAIM_ID,
	tefBAD_GEN_AUTH,
	tefBAD_LEDGER,
	tefCLAIMED,
	tefCREATED,
	tefGEN_IN_USE,
	tefPAST_SEQ,

	// -99 .. -1: R Retry (sequence too high, no funds for txn fee, originating account non-existent)
	// Transaction cannot be applied, cannot charge fee, not forwarded, might succeed later, hold
	terRETRY		= -99,
	terDIR_FULL,
	terFUNDS_SPENT,
	terINSUF_FEE_B,
	terNO_ACCOUNT,
	terNO_DST,
	terNO_LINE_NO_ZERO,
	terOFFER_NOT_FOUND, // XXX If we check sequence first this could be hard failure.
	terPRE_SEQ,
	terSET_MISSING_DST,
	terUNFUNDED,

	// 0: S Success (success)
	// Transaction succeeds, can be applied, can charge fee, forwarded
	tesSUCCESS		= 0,

	// 100 .. P Partial success (SR) (ripple transaction with no good paths, pay to non-existent account)
	// Transaction can be applied, can charge fee, forwarded, but does not achieve optimal result.
	tepPARITAL		= 100,
	tepPATH_DRY,
	tepPATH_PARTIAL,
};

bool transResultInfo(TransactionEngineResult terCode, std::string& strToken, std::string& strHuman);

enum TransactionEngineParams
{
	tepNONE          = 0,
	tepNO_CHECK_SIGN = 1,	// Signature already checked
	tepNO_CHECK_FEE  = 2,	// It was voted into a ledger anyway
	tepUPDATE_TOTAL  = 4,	// Update the total coins
	tepMETADATA      = 5,   // put metadata in tree, not transaction
};

typedef struct {
	uint16							uFlags;				// --> From path.

	uint160							uAccountID;			// --> Recieving/sending account.
	uint160							uCurrencyID;		// --> Accounts: receive and send, Offers: send.
														// --- For offer's next has currency out.
	uint160							uIssuerID;			// --> Currency's issuer

	// Computed by Reverse.
	STAmount						saRevRedeem;		// <-- Amount to redeem to next.
	STAmount						saRevIssue;			// <-- Amount to issue to next limited by credit and outstanding IOUs.
														//     Issue isn't used by offers.
	STAmount						saRevDeliver;		// <-- Amount to deliver to next regardless of fee.

	// Computed by forward.
	STAmount						saFwdRedeem;		// <-- Amount node will redeem to next.
	STAmount						saFwdIssue;			// <-- Amount node will issue to next.
														//	   Issue isn't used by offers.
	STAmount						saFwdDeliver;		// <-- Amount to deliver to next regardless of fee.
} paymentNode;

// Hold a path state under incremental application.
class PathState
{
protected:
	Ledger::pointer				mLedger;

	bool pushNode(int iType, uint160 uAccountID, uint160 uCurrencyID, uint160 uIssuerID);
	bool pushImply(uint160 uAccountID, uint160 uCurrencyID, uint160 uIssuerID);

public:
	typedef boost::shared_ptr<PathState> pointer;

	bool							bValid;
	std::vector<paymentNode>		vpnNodes;

	// When processing, don't want to complicate directory walking with deletion.
	std::vector<uint256>			vUnfundedBecame;	// Offers that became unfunded.

	// First time working in reverse a funding source was mentioned.  Source may only be used there.
	boost::unordered_map<std::pair<uint160, uint160>, int>	umSource;	// Map of currency, issuer to node index.

	LedgerEntrySet					lesEntries;

	int							mIndex;
	uint64						uQuality;		// 0 = none.
	STAmount					saInReq;		// Max amount to spend by sender
	STAmount					saInAct;		// Amount spent by sender (calc output)
	STAmount					saOutReq;		// Amount to send (calc input)
	STAmount					saOutAct;		// Amount actually sent (calc output).

	PathState(
		const Ledger::pointer&	lpLedger,
		const int				iIndex,
		const LedgerEntrySet&	lesSource,
		const STPath&			spSourcePath,
		const uint160&			uReceiverID,
		const uint160&			uSenderID,
		const STAmount&			saSend,
		const STAmount&			saSendMax,
		const bool				bPartialPayment
		);

	Json::Value	getJson() const;

	static PathState::pointer createPathState(
		const Ledger::pointer&	lpLedger,
		const int				iIndex,
		const LedgerEntrySet&	lesSource,
		const STPath&			spSourcePath,
		const uint160&			uReceiverID,
		const uint160&			uSenderID,
		const STAmount&			saSend,
		const STAmount&			saSendMax,
		const bool				bPartialPayment
		)
	{
		PathState::pointer	pspNew = boost::make_shared<PathState>(lpLedger, iIndex, lesSource, spSourcePath, uReceiverID, uSenderID, saSend, saSendMax, bPartialPayment);

		return pspNew && pspNew->bValid ? pspNew : PathState::pointer();
	}

	static bool lessPriority(const PathState::pointer& lhs, const PathState::pointer& rhs);
};

// One instance per ledger.
// Only one transaction applied at a time.
class TransactionEngine
{
private:
	LedgerEntrySet						mNodes;

	TransactionEngineResult dirAdd(
		uint64&							uNodeDir,		// Node of entry.
		const uint256&					uRootIndex,
		const uint256&					uLedgerIndex);

	TransactionEngineResult dirDelete(
		const bool						bKeepRoot,
		const uint64&					uNodeDir,		// Node item is mentioned in.
		const uint256&					uRootIndex,
		const uint256&					uLedgerIndex,	// Item being deleted
		const bool						bStable);

	bool dirFirst(const uint256& uRootIndex, SLE::pointer& sleNode, unsigned int& uDirEntry, uint256& uEntryIndex);
	bool dirNext(const uint256& uRootIndex, SLE::pointer& sleNode, unsigned int& uDirEntry, uint256& uEntryIndex);

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
	Ledger::pointer		mLedger;
	uint64				mLedgerParentCloseTime;

	uint160				mTxnAccountID;
	SLE::pointer		mTxnAccount;

	// First time working in reverse a funding source was mentioned.  Source may only be used there.
	boost::unordered_map<std::pair<uint160, uint160>, int>	mumSource;	// Map of currency, issuer to node index.

	// When processing, don't want to complicate directory walking with deletion.
	std::vector<uint256>			mvUnfundedBecame;	// Offers that became unfunded.

	// If the transaction fails to meet some constraint, still need to delete unfunded offers.
	boost::unordered_set<uint256>	musUnfundedFound;	// Offers that were found unfunded.

	SLE::pointer		entryCreate(LedgerEntryType letType, const uint256& uIndex);
	SLE::pointer		entryCache(LedgerEntryType letType, const uint256& uIndex);
	void				entryDelete(SLE::pointer sleEntry, bool bUnfunded = false);
	void				entryModify(SLE::pointer sleEntry);

	TransactionEngineResult offerDelete(const uint256& uOfferIndex);
	TransactionEngineResult offerDelete(const SLE::pointer& sleOffer, const uint256& uOfferIndex, const uint160& uOwnerID);

	uint32				rippleTransferRate(const uint160& uIssuerID);
	STAmount			rippleOwed(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID);
	STAmount			rippleLimit(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID);
	uint32				rippleQualityIn(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID, const SOE_Field sfLow=sfLowQualityIn, const SOE_Field sfHigh=sfHighQualityIn);
	uint32				rippleQualityOut(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID)
	{ return rippleQualityIn(uToAccountID, uFromAccountID, uCurrencyID, sfLowQualityOut, sfHighQualityOut); }

	STAmount			rippleHolds(const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID);
	STAmount			rippleTransferFee(const uint160& uSenderID, const uint160& uReceiverID, const uint160& uIssuerID, const STAmount& saAmount);
	void				rippleCredit(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount, bool bCheckIssuer=true);
	STAmount			rippleSend(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount);

	STAmount			accountHolds(const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID);
	STAmount			accountSend(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount);
	STAmount			accountFunds(const uint160& uAccountID, const STAmount& saDefault);

	PathState::pointer	pathCreate(const STPath& spPath);
	void				pathNext(PathState::pointer pspCur, int iPaths);
	bool				calcNode(unsigned int uIndex, PathState::pointer pspCur, bool bMultiQuality);
	bool				calcNodeOfferRev(unsigned int uIndex, PathState::pointer pspCur, bool bMultiQuality);
	bool				calcNodeOfferFwd(unsigned int uIndex, PathState::pointer pspCur, bool bMultiQuality);
	bool				calcNodeAccountRev(unsigned int uIndex, PathState::pointer pspCur, bool bMultiQuality);
	bool				calcNodeAccountFwd(unsigned int uIndex, PathState::pointer pspCur, bool bMultiQuality);
	void				calcNodeRipple(const uint32 uQualityIn, const uint32 uQualityOut,
							const STAmount& saPrvReq, const STAmount& saCurReq,
							STAmount& saPrvAct, STAmount& saCurAct);

	void				txnWrite();

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
	TransactionEngine(const Ledger::pointer& ledger) : mLedger(ledger) { ; }

	Ledger::pointer getLedger()						{ return mLedger; }
	void setLedger(const Ledger::pointer& ledger)	{ assert(ledger); mLedger = ledger; }

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
