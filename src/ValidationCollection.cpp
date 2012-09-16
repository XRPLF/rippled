
#include "ValidationCollection.h"

#include <boost/foreach.hpp>

#include "Application.h"
#include "LedgerTiming.h"
#include "Log.h"

// #define VC_DEBUG

bool ValidationCollection::addValidation(const SerializedValidation::pointer& val)
{
	NewcoinAddress signer = val->getSignerPublic();
	bool isCurrent = false;
	if (theApp->getUNL().nodeInUNL(signer) || val->isTrusted())
	{
		val->setTrusted();
		uint32 now = theApp->getOPs().getCloseTimeNC();
		uint32 valClose = val->getSignTime();
		if ((now > (valClose - LEDGER_EARLY_INTERVAL)) && (now < (valClose + LEDGER_VAL_INTERVAL)))
			isCurrent = true;
		else
			Log(lsWARNING) << "Received stale validation now=" << now << ", close=" << valClose;
	}
	else Log(lsINFO) << "Node " << signer.humanNodePublic() << " not in UNL";

	uint256 hash = val->getLedgerHash();
	uint160 node = signer.getNodeID();

	{
		boost::mutex::scoped_lock sl(mValidationLock);
		if (!mValidations[hash].insert(std::make_pair(node, val)).second)
			return false;
		if (isCurrent)
		{
			boost::unordered_map<uint160, SerializedValidation::pointer>::iterator it = mCurrentValidations.find(node);
			if (it == mCurrentValidations.end())
				mCurrentValidations.insert(std::make_pair(node, val));
			else if (!it->second)
				it->second = val;
			else if (val->getSignTime() > it->second->getSignTime())
			{
				val->setPreviousHash(it->second->getLedgerHash());
				mStaleValidations.push_back(it->second);
				it->second = val;
				condWrite();
			}
		}
	}

	Log(lsINFO) << "Val for " << hash << " from " << signer.humanNodePublic()
		<< " added " << (val->isTrusted() ? "trusted/" : "UNtrusted/") << (isCurrent ? "current" : "stale");
	return isCurrent;
}

ValidationSet ValidationCollection::getValidations(const uint256& ledger)
{
	ValidationSet ret;
	{
		boost::mutex::scoped_lock sl(mValidationLock);
		boost::unordered_map<uint256, ValidationSet>::iterator it = mValidations.find(ledger);
		if (it != mValidations.end())
			ret = it->second;
	}
	return ret;
}

void ValidationCollection::getValidationCount(const uint256& ledger, bool currentOnly, int& trusted, int &untrusted)
{
	trusted = untrusted = 0;
	boost::mutex::scoped_lock sl(mValidationLock);
	boost::unordered_map<uint256, ValidationSet>::iterator it = mValidations.find(ledger);
	uint32 now = theApp->getOPs().getNetworkTimeNC();
	if (it != mValidations.end())
	{
		for (ValidationSet::iterator vit = it->second.begin(), end = it->second.end(); vit != end; ++vit)
		{
			bool isTrusted = vit->second->isTrusted();
			if (isTrusted && currentOnly)
			{
				uint32 closeTime = vit->second->getSignTime();
				if ((now < (closeTime - LEDGER_EARLY_INTERVAL)) || (now > (closeTime + LEDGER_VAL_INTERVAL)))
					isTrusted = false;
				else
				{
#ifdef VC_DEBUG
					Log(lsINFO) << "VC: Untrusted due to time " << ledger;
#endif
				}
			}
			if (isTrusted)
				++trusted;
			else
				++untrusted;
		}
	}
#ifdef VC_DEBUG
	Log(lsINFO) << "VC: " << ledger << "t:" << trusted << " u:" << untrusted;
#endif
}

int ValidationCollection::getTrustedValidationCount(const uint256& ledger)
{
	int trusted = 0;
	boost::mutex::scoped_lock sl(mValidationLock);
	for (boost::unordered_map<uint256, ValidationSet>::iterator it = mValidations.find(ledger),
		end = mValidations.end(); it != end; ++it)
	{
		for (ValidationSet::iterator vit = it->second.begin(), end = it->second.end(); vit != end; ++vit)
		{
			if (vit->second->isTrusted())
				++trusted;
		}
	}
	return trusted;
}

int ValidationCollection::getNodesAfter(const uint256& ledger)
{ // Number of trusted nodes that have moved past this ledger
	int count = 0;
	boost::mutex::scoped_lock sl(mValidationLock);
	for (boost::unordered_map<uint160, SerializedValidation::pointer>::iterator it = mCurrentValidations.begin(),
		end = mCurrentValidations.end(); it != end; ++it)
	{
		if (it->second->isTrusted() && it->second->isPreviousHash(ledger))
			++count;
	}
	return count;
}

int ValidationCollection::getLoadRatio(bool overLoaded)
{ // how many trusted nodes are able to keep up, higher is better
	int goodNodes = overLoaded ? 1 : 0;
	int badNodes = overLoaded ? 0 : 1;
	{
		boost::mutex::scoped_lock sl(mValidationLock);
		for (boost::unordered_map<uint160, SerializedValidation::pointer>::iterator it = mCurrentValidations.begin(),
			end = mCurrentValidations.end(); it != end; ++it)
		{
			if (it->second->isTrusted())
			{
				if (it->second->isFull())
					++goodNodes;
				else
					++badNodes;
			}
		}
	}
	return (goodNodes * 100) / (goodNodes + badNodes);
}

boost::unordered_map<uint256, int> ValidationCollection::getCurrentValidations(uint256 currentLedger)
{
    uint32 cutoff = theApp->getOPs().getNetworkTimeNC() - LEDGER_VAL_INTERVAL;
    bool valCurrentLedger = currentLedger.isNonZero();

	boost::unordered_map<uint256, int> ret;

	{
		boost::mutex::scoped_lock sl(mValidationLock);
		boost::unordered_map<uint160, SerializedValidation::pointer>::iterator it = mCurrentValidations.begin();
		while (it != mCurrentValidations.end())
		{
			if (!it->second) // contains no record
				it = mCurrentValidations.erase(it);
			else if (it->second->getSignTime() < cutoff)
			{ // contains a stale record
				mStaleValidations.push_back(it->second);
				it->second = SerializedValidation::pointer();
				condWrite();
				it = mCurrentValidations.erase(it);
			}
			else
			{ // contains a live record
				if (valCurrentLedger && it->second->isPreviousHash(currentLedger))
					++ret[currentLedger]; // count for the favored ledger
				else
					++ret[it->second->getLedgerHash()];
				++it;
			}
		}
	}

	return ret;
}

void ValidationCollection::flush()
{
		boost::mutex::scoped_lock sl(mValidationLock);
		boost::unordered_map<uint160, SerializedValidation::pointer>::iterator it = mCurrentValidations.begin();
		bool anyNew = false;
		while (it != mCurrentValidations.end())
		{
			if (it->second)
				mStaleValidations.push_back(it->second);
			++it;
			anyNew = true;
		}
		mCurrentValidations.clear();
		if (anyNew)
			condWrite();
		while (mWriting)
		{
			sl.unlock();
			boost::this_thread::sleep(boost::posix_time::milliseconds(100));
			sl.lock();
		}
}

void ValidationCollection::condWrite()
{
	if (mWriting)
		return;
	mWriting = true;
	boost::thread thread(boost::bind(&ValidationCollection::doWrite, this));
	thread.detach();
}

void ValidationCollection::doWrite()
{
	static boost::format insVal("INSERT INTO LedgerValidations "
		"(LedgerHash,NodePubKey,Flags,SignTime,Signature) VALUES ('%s','%s','%u','%u',%s);");

	boost::mutex::scoped_lock sl(mValidationLock);
	assert(mWriting);
	while (!mStaleValidations.empty())
	{
		std::vector<SerializedValidation::pointer> vector;
		mStaleValidations.swap(vector);
		sl.unlock();
		{
			Database *db = theApp->getLedgerDB()->getDB();
			ScopedLock dbl(theApp->getLedgerDB()->getDBLock());

			db->executeSQL("BEGIN TRANSACTION;");
			BOOST_FOREACH(const SerializedValidation::pointer& it, vector)
				db->executeSQL(boost::str(insVal % it->getLedgerHash().GetHex()
					% it->getSignerPublic().humanNodePublic() % it->getFlags() % it->getSignTime()
					% db->escape(strCopy(it->getSignature()))));
			db->executeSQL("END TRANSACTION;");
		}

		sl.lock();
	}
	mWriting = false;
}
