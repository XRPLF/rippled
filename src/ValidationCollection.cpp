
#include "ValidationCollection.h"

#include "Application.h"
#include "LedgerTiming.h"
#include "Log.h"

bool ValidationCollection::addValidation(SerializedValidation::pointer val)
{
	bool isTrusted = false;
	if (theApp->getUNL().nodeInUNL(val->getSignerPublic()))
	{
		uint64 now = theApp->getOPs().getNetworkTimeNC();
		uint64 valClose = val->getCloseTime();
		if ((now > valClose) && (now < (valClose + 2 * LEDGER_INTERVAL)))
			isTrusted = true;
		else
		Log(lsWARNING) << "Received stale validation now=" << now << ", close=" << valClose;
	}

	uint256 hash = val->getLedgerHash();
	uint160 node = val->getSignerPublic().getNodeID();

	{
		boost::mutex::scoped_lock sl(mValidationLock);
		if (!mValidations[hash].insert(std::make_pair(node, val)).second)
			return false;
		if (isTrusted)
		{
			boost::unordered_map<uint160, SerializedValidation::pointer>::iterator it = mCurrentValidations.find(node);
			if ((it == mCurrentValidations.end()) || (val->getCloseTime() >= it->second->getCloseTime()))
				mCurrentValidations[node] = val;
		}
	}

	Log(lsINFO) << "Val for " << hash.GetHex() << " from " << node.GetHex() << " added " <<
		(val->isTrusted() ? "trusted" : "UNtrusted");
	return true;
}

ValidationSet ValidationCollection::getValidations(const uint256& ledger)
{
	ValidationSet ret;
	{
		boost::mutex::scoped_lock sl(mValidationLock);
		boost::unordered_map<uint256, ValidationSet>::iterator it = mValidations.find(ledger);
		if (it != mValidations.end()) ret = it->second;
	}
	return ret;
}

void ValidationCollection::getValidationCount(const uint256& ledger, bool currentOnly, int& trusted, int &untrusted)
{
	trusted = untrusted = 0;
	boost::mutex::scoped_lock sl(mValidationLock);
	boost::unordered_map<uint256, ValidationSet>::iterator it = mValidations.find(ledger);
	uint64 now = theApp->getOPs().getNetworkTimeNC();
	if (it != mValidations.end())
	{
		for (ValidationSet::iterator vit = it->second.begin(), end = it->second.end(); vit != end; ++vit)
		{
			bool trusted = vit->second->isTrusted();
			if (trusted && currentOnly)
			{
				uint64 closeTime = vit->second->getCloseTime();
				if ((now < closeTime) || (now > (closeTime + 2 * LEDGER_INTERVAL)))
					trusted = false;
			}
			if (trusted)
				++trusted;
			else
				++untrusted;
		}
	}
}
