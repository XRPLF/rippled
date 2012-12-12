#ifndef __RIPPLE_CALC__
#define __RIPPLE_CALC__

#include <boost/unordered_set.hpp>
#include <boost/tuple/tuple.hpp>

#include "LedgerEntrySet.h"

class PaymentNode {
protected:
	friend class RippleCalc;
	friend class PathState;

	uint16							uFlags;				// --> From path.

	uint160							uAccountID;			// --> Accounts: Recieving/sending account.
	uint160							uCurrencyID;		// --> Accounts: Receive and send, Offers: send.
														// --- For offer's next has currency out.
	uint160							uIssuerID;			// --> Currency's issuer

	STAmount						saTransferRate;		// Transfer rate for uIssuerID.

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

	// For offers:

	STAmount						saRateMax;

	// Directory
	uint256							uDirectTip;			// Current directory.
	uint256							uDirectEnd;			// Next order book.
	bool							bDirectAdvance;		// Need to advance directory.
	SLE::pointer					sleDirectDir;
	STAmount						saOfrRate;			// For correct ratio.

	// Node
	bool							bEntryAdvance;		// Need to advance entry.
	unsigned int					uEntry;
	uint256							uOfferIndex;
	SLE::pointer					sleOffer;
	uint160							uOfrOwnerID;
	bool							bFundsDirty;		// Need to refresh saOfferFunds, saTakerPays, & saTakerGets.
	STAmount						saOfferFunds;
	STAmount						saTakerPays;
	STAmount						saTakerGets;

public:
	bool operator==(const PaymentNode& pnOther) const;
};

// account id, currency id, issuer id :: node
typedef boost::tuple<uint160, uint160, uint160> aciSource;
typedef boost::unordered_map<aciSource, unsigned int>					curIssuerNode;	// Map of currency, issuer to node index.
typedef boost::unordered_map<aciSource, unsigned int>::const_iterator	curIssuerNodeConstIterator;

extern std::size_t hash_value(const aciSource& asValue);

// Holds a path state under incremental application.
class PathState
{
protected:
	Ledger::ref					mLedger;

	TER		pushNode(const int iType, const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID);
	TER		pushImply(const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID);

public:
	typedef boost::shared_ptr<PathState>		pointer;
	typedef const boost::shared_ptr<PathState>&	ref;

	TER							terStatus;
	std::vector<PaymentNode>	vpnNodes;

	// When processing, don't want to complicate directory walking with deletion.
	std::vector<uint256>		vUnfundedBecame;	// Offers that became unfunded or were completely consumed.

	// First time scanning foward, as part of path contruction, a funding source was mentioned for accounts. Source may only be
	// used there.
	curIssuerNode				umForward;			// Map of currency, issuer to node index.

	// First time working in reverse a funding source was used.
	// Source may only be used there if not mentioned by an account.
	curIssuerNode				umReverse;			// Map of currency, issuer to node index.

	LedgerEntrySet				lesEntries;

	int							mIndex;				// Index/rank amoung siblings.
	uint64						uQuality;			// 0 = no quality/liquity left.
	const STAmount&				saInReq;			// --> Max amount to spend by sender.
	STAmount					saInAct;			// --> Amount spent by sender so far.
	STAmount					saInPass;			// <-- Amount spent by sender.
	const STAmount&				saOutReq;			// --> Amount to send.
	STAmount					saOutAct;			// --> Amount actually sent so far.
	STAmount					saOutPass;			// <-- Amount actually sent.
	bool						bConsumed;			// If true, use consumes full liquidity. False, may or may not.

	PathState*	setIndex(const int iIndex) {
		mIndex	= iIndex;

		return this;
	}

	int getIndex() { return mIndex; };

	PathState(
		const STAmount&			saSend,
		const STAmount&			saSendMax,
		const Ledger::ref		lrLedger = Ledger::pointer()
	) : mLedger(lrLedger), saInReq(saSendMax), saOutReq(saSend) { ; }

	PathState(const PathState& psSrc, bool bUnsed)
	 : mLedger(psSrc.mLedger), saInReq(psSrc.saInReq), saOutReq(psSrc.saOutReq) { ; }

	void setExpanded(
		const LedgerEntrySet&	lesSource,
		const STPath&			spSourcePath,
		const uint160&			uReceiverID,
		const uint160&			uSenderID
		);

	void setCanonical(
		const PathState&		psExpanded
		);

	Json::Value	getJson() const;

#if 0
	static PathState::pointer createCanonical(
		PathState&ref		pspExpanded
		)
	{
		PathState::pointer	pspNew	= boost::make_shared<PathState>(pspExpanded->saOutAct, pspExpanded->saInAct);

		pspNew->setCanonical(pspExpanded);

		return pspNew;
	}
#endif
	static bool lessPriority(PathState& lhs, PathState& rhs);
};

class RippleCalc
{
protected:
	LedgerEntrySet&					lesActive;

public:
	// First time working in reverse a funding source was mentioned.  Source may only be used there.
	curIssuerNode					mumSource;			// Map of currency, issuer to node index.

	// If the transaction fails to meet some constraint, still need to delete unfunded offers.
	boost::unordered_set<uint256>	musUnfundedFound;	// Offers that were found unfunded.

	void				pathNext(PathState::ref psrCur, const int iPaths, const LedgerEntrySet& lesCheckpoint, LedgerEntrySet& lesCurrent);
	TER					calcNode(const unsigned int uNode, PathState& psCur, const bool bMultiQuality);
	TER					calcNodeRev(const unsigned int uNode, PathState& psCur, const bool bMultiQuality);
	TER					calcNodeFwd(const unsigned int uNode, PathState& psCur, const bool bMultiQuality);
	TER					calcNodeOfferRev(const unsigned int uNode, PathState& psCur, const bool bMultiQuality);
	TER					calcNodeOfferFwd(const unsigned int uNode, PathState& psCur, const bool bMultiQuality);
	TER					calcNodeAccountRev(const unsigned int uNode, PathState& psCur, const bool bMultiQuality);
	TER					calcNodeAccountFwd(const unsigned int uNode, PathState& psCur, const bool bMultiQuality);
	TER					calcNodeAdvance(const unsigned int uNode, PathState& psCur, const bool bMultiQuality, const bool bReverse);
	TER					calcNodeDeliverRev(
							const unsigned int			uNode,
							PathState&					psCur,
							const bool					bMultiQuality,
							const uint160&				uOutAccountID,
							const STAmount&				saOutReq,
							STAmount&					saOutAct);

	TER					calcNodeDeliverFwd(
							const unsigned int			uNode,
							PathState&					psCur,
							const bool					bMultiQuality,
							const uint160&				uInAccountID,
							const STAmount&				saInReq,
							STAmount&					saInAct,
							STAmount&					saInFees);

	void				calcNodeRipple(const uint32 uQualityIn, const uint32 uQualityOut,
							const STAmount& saPrvReq, const STAmount& saCurReq,
							STAmount& saPrvAct, STAmount& saCurAct,
							uint64& uRateMax);

	RippleCalc(LedgerEntrySet& lesNodes) : lesActive(lesNodes) { ; }

	static TER rippleCalc(
		LedgerEntrySet&					lesActive,
			  STAmount&					saMaxAmountAct,
			  STAmount&					saDstAmountAct,
			  std::vector<PathState::pointer>&	vpsExpanded,
		const STAmount&					saDstAmountReq,
		const STAmount&					saMaxAmountReq,
		const uint160&					uDstAccountID,
		const uint160&					uSrcAccountID,
		const STPathSet&				spsPaths,
		const bool						bPartialPayment,
		const bool						bLimitQuality,
		const bool						bNoRippleDirect,
		const bool						bStandAlone
		);

	static void setCanonical(STPathSet& spsDst, const std::vector<PathState::pointer>& vpsExpanded, bool bKeepDefault);
};

#endif
// vim:ts=4
