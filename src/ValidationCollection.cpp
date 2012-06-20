
#include "ValidationCollection.h"

#include "Application.h"
#include "Log.h"

bool ValidationCollection::addValidation(SerializedValidation::pointer val)
{
	if(theApp->getUNL().nodeInUNL(val->getSignerPublic()))
		val->setTrusted();

	uint256 hash = val->getLedgerHash();
	uint160 node = val->getSignerPublic().getNodeID();

	{
		boost::mutex::scoped_lock sl(mValidationLock);
		if (!mValidations[hash].insert(std::make_pair(node, val)).second)
			return false;
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

void ValidationCollection::getValidationCount(const uint256& ledger, int& trusted, int &untrusted)
{
	trusted = untrusted = 0;
	boost::mutex::scoped_lock sl(mValidationLock);
	boost::unordered_map<uint256, ValidationSet>::iterator it = mValidations.find(ledger);
	if (it != mValidations.end())
	{
		for (ValidationSet::iterator vit = it->second.begin(), end = it->second.end(); vit != end; ++vit)
		{
			if (vit->second->isTrusted())
				++trusted;
			else
				++untrusted;
		}
	}
}
