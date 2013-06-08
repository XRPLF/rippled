#ifndef __OFFERCREATETRANSACTOR__
#define __OFFERCREATETRANSACTOR__

#include "Transactor.h"

class OfferCreateTransactor : public Transactor
{
public:
	OfferCreateTransactor (const SerializedTransaction& txn,TransactionEngineParams params, TransactionEngine* engine) : Transactor(txn,params,engine) {}
	TER doApply();

private:
	bool bValidOffer(
		SLE::ref			sleOfferDir,
		uint256 const& 		uOffer,
		const uint160&		uOfferOwnerID,
		const STAmount&		saOfferPays,
		const STAmount&		saOfferGets,
		const uint160&		uTakerAccountID,
		boost::unordered_set<uint256>&	usOfferUnfundedFound,
		boost::unordered_set<uint256>&	usOfferUnfundedBecame,
		boost::unordered_set<uint160>&	usAccountTouched,
		STAmount&			saOfferFunds);

	TER takeOffers(
		const bool			bOpenLedger,
		const bool			bPassive,
		const bool			bSell,
		uint256 const& 		uBookBase,
		const uint160&		uTakerAccountID,
		SLE::ref			sleTakerAccount,
		const STAmount&		saTakerPays,
		const STAmount&		saTakerGets,
		STAmount&			saTakerPaid,
		STAmount&			saTakerGot,
		bool&				bUnfunded);

	boost::unordered_set<uint256>	usOfferUnfundedFound;	// Offers found unfunded.

};

#endif

// vim:ts=4
