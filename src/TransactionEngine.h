#ifndef __TRANSACTIONENGINE__
#define __TRANSACTIONENGINE__

#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/unordered_set.hpp>
#include <boost/unordered_map.hpp>

#include "Ledger.h"
#include "SerializedTransaction.h"
#include "SerializedLedger.h"
#include "LedgerEntrySet.h"

// A TransactionEngine applies serialized transactions to a ledger
// It can also, verify signatures, verify fees, and give rejection reasons

enum TER	// aka TransactionEngineResult
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
	temBAD_PATH,
	temBAD_PATH_LOOP,
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
	tefEXCEPTION,
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
	terNO_LINE,
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
	tepPARTIAL		= 100,
	tepPATH_DRY,
	tepPATH_PARTIAL,
};

bool transResultInfo(TER terCode, std::string& strToken, std::string& strHuman);

enum TransactionEngineParams
{
	tapNONE				= 0x00,

	tapNO_CHECK_SIGN	= 0x01,	// Signature already checked

	tapOPEN_LEDGER		= 0x10,	// Transaction is running against an open ledger
		// true = failures are not forwarded, check transaction fee
		// false = debit ledger for consumed funds

	tapRETRY			= 0x20,	// This is not the transaction's last pass
		// Transaction can be retried, soft failures allowed
};

typedef struct {
	uint16							uFlags;				// --> From path.

	uint160							uAccountID;			// --> Accounts: Recieving/sending account.
	uint160							uCurrencyID;		// --> Accounts: Receive and send, Offers: send.
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

// account id, currency id, issuer id :: node
typedef boost::tuple<uint160, uint160, uint160> aciSource;
typedef boost::unordered_map<aciSource, unsigned int>					curIssuerNode;	// Map of currency, issuer to node index.
typedef boost::unordered_map<aciSource, unsigned int>::const_iterator	curIssuerNodeConstIterator;

extern std::size_t hash_value(const aciSource& asValue);
// extern std::size_t hash_value(const boost::tuple<uint160, uint160, uint160>& bt);

// Hold a path state under incremental application.
class PathState
{
protected:
	Ledger::pointer				mLedger;

	TER		pushNode(int iType, uint160 uAccountID, uint160 uCurrencyID, uint160 uIssuerID);
	TER		pushImply(uint160 uAccountID, uint160 uCurrencyID, uint160 uIssuerID);

public:
	typedef boost::shared_ptr<PathState> pointer;

	TER							terStatus;
	std::vector<paymentNode>	vpnNodes;

	// When processing, don't want to complicate directory walking with deletion.
	std::vector<uint256>		vUnfundedBecame;	// Offers that became unfunded.

	// First time working foward a funding source was mentioned for accounts. Source may only be used there.
	curIssuerNode				umForward;	// Map of currency, issuer to node index.

	// First time working in reverse a funding source was used.
	// Source may only be used there if not mentioned by an account.
	curIssuerNode				umReverse;	// Map of currency, issuer to node index.

	LedgerEntrySet				lesEntries;

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
		return boost::make_shared<PathState>(lpLedger, iIndex, lesSource, spSourcePath, uReceiverID, uSenderID, saSend, saSendMax, bPartialPayment);
	}

	static bool lessPriority(const PathState::pointer& lhs, const PathState::pointer& rhs);
};

// One instance per ledger.
// Only one transaction applied at a time.
class TransactionEngine
{
private:
	LedgerEntrySet						mNodes;

	TER dirAdd(
		uint64&							uNodeDir,		// Node of entry.
		const uint256&					uRootIndex,
		const uint256&					uLedgerIndex);

	TER dirDelete(
		const bool						bKeepRoot,
		const uint64&					uNodeDir,		// Node item is mentioned in.
		const uint256&					uRootIndex,
		const uint256&					uLedgerIndex,	// Item being deleted
		const bool						bStable);

	bool dirFirst(const uint256& uRootIndex, SLE::pointer& sleNode, unsigned int& uDirEntry, uint256& uEntryIndex);
	bool dirNext(const uint256& uRootIndex, SLE::pointer& sleNode, unsigned int& uDirEntry, uint256& uEntryIndex);

	TER	setAuthorized(const SerializedTransaction& txn, bool bMustSetGenerator);

	TER takeOffers(
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
	curIssuerNode		mumSource;	// Map of currency, issuer to node index.

	// When processing, don't want to complicate directory walking with deletion.
	std::vector<uint256>			mvUnfundedBecame;	// Offers that became unfunded.

	// If the transaction fails to meet some constraint, still need to delete unfunded offers.
	boost::unordered_set<uint256>	musUnfundedFound;	// Offers that were found unfunded.

	SLE::pointer		entryCreate(LedgerEntryType letType, const uint256& uIndex);
	SLE::pointer		entryCache(LedgerEntryType letType, const uint256& uIndex);
	void				entryDelete(SLE::pointer sleEntry, bool bUnfunded = false);
	void				entryModify(SLE::pointer sleEntry);

	TER					offerDelete(const uint256& uOfferIndex);
	TER					offerDelete(const SLE::pointer& sleOffer, const uint256& uOfferIndex, const uint160& uOwnerID);

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
	void				pathNext(const PathState::pointer& pspCur, const int iPaths);
	TER					calcNode(const unsigned int uIndex, const PathState::pointer& pspCur, const bool bMultiQuality);
	TER					calcNodeOfferRev(const unsigned int uIndex, const PathState::pointer& pspCur, const bool bMultiQuality);
	TER					calcNodeOfferFwd(const unsigned int uIndex, const PathState::pointer& pspCur, const bool bMultiQuality);
	TER					calcNodeAccountRev(const unsigned int uIndex, const PathState::pointer& pspCur, const bool bMultiQuality);
	TER					calcNodeAccountFwd(const unsigned int uIndex, const PathState::pointer& pspCur, const bool bMultiQuality);
	void				calcNodeRipple(const uint32 uQualityIn, const uint32 uQualityOut,
							const STAmount& saPrvReq, const STAmount& saCurReq,
							STAmount& saPrvAct, STAmount& saCurAct);

	void				txnWrite();

	TER					doAccountSet(const SerializedTransaction& txn);
	TER					doClaim(const SerializedTransaction& txn);
	TER					doCreditSet(const SerializedTransaction& txn);
	TER					doDelete(const SerializedTransaction& txn);
	TER					doInvoice(const SerializedTransaction& txn);
	TER					doOfferCreate(const SerializedTransaction& txn);
	TER					doOfferCancel(const SerializedTransaction& txn);
	TER					doNicknameSet(const SerializedTransaction& txn);
	TER					doPasswordFund(const SerializedTransaction& txn);
	TER					doPasswordSet(const SerializedTransaction& txn);
	TER					doPayment(const SerializedTransaction& txn);
	TER					doStore(const SerializedTransaction& txn);
	TER					doTake(const SerializedTransaction& txn);
	TER					doWalletAdd(const SerializedTransaction& txn);

public:
	TransactionEngine() { ; }
	TransactionEngine(const Ledger::pointer& ledger) : mLedger(ledger) { assert(mLedger); }

	Ledger::pointer getLedger()						{ return mLedger; }
	void setLedger(const Ledger::pointer& ledger)	{ assert(ledger); mLedger = ledger; }

	TER applyTransaction(const SerializedTransaction&, TransactionEngineParams);
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
