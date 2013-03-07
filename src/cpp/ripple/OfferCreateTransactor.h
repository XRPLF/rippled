#ifndef __OFFERCREATETRANSACTOR__
#define __OFFERCREATETRANSACTOR__

#include "Transactor.h"

class OfferCreateTransactor : public Transactor
{
	TER takeOffers(
		const bool			bOpenLedger,
		const bool			bPassive,
		const uint256&		uBookBase,
		const uint160&		uTakerAccountID,
		SLE::ref			sleTakerAccount,
		const STAmount&		saTakerPays,
		const STAmount&		saTakerGets,
		STAmount&			saTakerPaid,
		STAmount&			saTakerGot,
		bool&				bUnfunded);

public:
	OfferCreateTransactor(const SerializedTransaction& txn,TransactionEngineParams params, TransactionEngine* engine) : Transactor(txn,params,engine) {}
	TER doApply();
};
#endif

// vim:ts=4
