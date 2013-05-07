
#include "TransactionErr.h"
#include "TransactionEngine.h"

// Double check a transaction's metadata to make sure no system invariants were broken

SETUP_LOG();

bool TransactionEngine::checkInvariants(TER result, const SerializedTransaction& txn, TransactionEngineParams params)
{
#if 0
	const RippleAddress&	srcAccount	= txn.getFieldAccount(sfAccount);
	uint32					txnSeq		= txn.getFieldU32(sfSequence);

	LedgerEntryAction		leaAction;

	uint256					srcActId	= Ledger::getAccountRootIndex(srcAccount.getAccountID());
	SLE::pointer			origSrcAct	= mLedger->getSLE(srcActId);
	SLE::pointer			newSrcAct	= mNodes.getEntry(srcActId, leaAction);

	if (!newSrcAct || !origSrcAct)
	{
		cLog(lsFATAL) << "Transaction created or destroyed its issuing account";
		assert(false);
		return tefINTERNAL;
	}

	if ((newSrcAct->getFieldU32(sfSequence) != (txnSeq + 1)) ||
		(origSrcAct->getFieldU32(sfSequence) != txnSeq))
	{
		cLog(lsFATAL) << "Transaction mangles sequence numbers";
		cLog(lsFATAL) << "t:" << txnSeq << " o: " << origSrcAct->getFieldU32(sfSequence)
			<< " n: " << newSrcAct->getFieldU32(sfSequence);
		assert(false);
		return tefINTERNAL;
	}

	int64 xrpChange = txn.getFieldAmount(sfFee).getSNValue();
	for (LedgerEntrySet::const_iterator it = mNodes.begin(), end = mNodes.end(); it != end; ++it)
	{
		const LedgerEntrySetEntry& entry = it->second;
		if (entry.mAction == taaMODIFY)
		{
#if 0
			if (entry.mEntry->getType() == ltRIPPLE_STATE)
			{
				// if this transaction pushes a ripple state over its limit, make sure it also modifies
				// an offer placed by that same user
			}
#endif
			if (entry.mEntry->getType() == ltACCOUNT_ROOT)
			{ // account modified
				xrpChange += entry.mEntry->getFieldAmount(sfBalance).getSNValue();
				xrpChange -= mLedger->getSLE(it->first)->getFieldAmount(sfBalance).getSNValue();
			}
		}
		else if (entry.mAction == taaCREATE)
		{
			if (entry.mEntry->getType() == ltACCOUNT_ROOT) // account created
				xrpChange += entry.mEntry->getFieldAmount(sfBalance).getSNValue();
		}
	}
	if (xrpChange != 0)
	{
		cLog(lsFATAL) << "Transaction creates/destroys XRP";
		assert(false);
		return tefINTERNAL;
	}
#endif

	return true;
}
