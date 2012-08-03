
#include "ValidationCollection.h"

#include "Application.h"
#include "LedgerTiming.h"
#include "Log.h"

bool ValidationCollection::addValidation(SerializedValidation::pointer val)
{
	NewcoinAddress signer = val->getSignerPublic();
	bool isCurrent = false;
	if (theApp->getUNL().nodeInUNL(signer))
	{
		val->setTrusted();
		uint32 now = theApp->getOPs().getNetworkTimeNC();
		uint32 valClose = val->getCloseTime();
		if ((now > valClose) && (now < (valClose + LEDGER_MAX_INTERVAL)))
			isCurrent = true;
		else
			Log(lsWARNING) << "Received stale validation now=" << now << ", close=" << valClose;
	}

	uint256 hash = val->getLedgerHash();
	uint160 node = signer.getNodeID();

	{
		boost::mutex::scoped_lock sl(mValidationLock);
		if (!mValidations[hash].insert(std::make_pair(node, val)).second)
			return false;
		if (isCurrent)
		{
			boost::unordered_map<uint160, SerializedValidation::pointer>::iterator it = mCurrentValidations.find(node);
			if ((it == mCurrentValidations.end()) || (val->getCloseTime() >= it->second->getCloseTime()))
			{
				if (it != mCurrentValidations.end())
					mStaleValidations.push_back(it->second);
				mCurrentValidations[node] = val;
				condWrite();
			}
		}
	}

	Log(lsINFO) << "Val for " << hash.GetHex() << " from " << signer.humanNodePublic()
		<< " added " << (val->isTrusted() ? "trusted" : "UNtrusted");
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
				uint32 closeTime = vit->second->getCloseTime();
				if ((now < closeTime) || (now > (closeTime + 2 * LEDGER_MAX_INTERVAL)))
					isTrusted = false;
			}
			if (isTrusted)
				++trusted;
			else
				++untrusted;
		}
	}
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

int ValidationCollection::getCurrentValidationCount(uint32 afterTime)
{
	int count = 0;
	boost::mutex::scoped_lock sl(mValidationLock);
	for (boost::unordered_map<uint160, SerializedValidation::pointer>::iterator it = mCurrentValidations.begin(),
		end = mCurrentValidations.end(); it != end; ++it)
	{
		if (it->second->isTrusted() && (it->second->getCloseTime() > afterTime))
			++count;
	}
	return count;
}

boost::unordered_map<uint256, int> ValidationCollection::getCurrentValidations()
{
    uint32 now = theApp->getOPs().getNetworkTimeNC();
	boost::unordered_map<uint256, int> ret;

	{
		boost::mutex::scoped_lock sl(mValidationLock);
		boost::unordered_map<uint160, SerializedValidation::pointer>::iterator it = mCurrentValidations.begin();
		bool anyNew = false;
		while (it != mCurrentValidations.end())
		{
			if (now > (it->second->getCloseTime() + LEDGER_MAX_INTERVAL))
			{
				mStaleValidations.push_back(it->second);
				it = mCurrentValidations.erase(it);
				anyNew = true;
			}
			else
			{
				++ret[it->second->getLedgerHash()];
				++it;
			}
		}
		if (anyNew)
			condWrite();
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
		"(LedgerHash,NodePubKey,Flags,CloseTime,Signature) VALUES "
		"('%s','%s','%u','%u',%s);");

	boost::mutex::scoped_lock sl(mValidationLock);
	assert(mWriting);
	while (!mStaleValidations.empty())
	{
		std::vector<SerializedValidation::pointer> vector;
		mStaleValidations.swap(vector);
		sl.unlock();

		{
			ScopedLock dbl(theApp->getLedgerDB()->getDBLock());
			Database *db = theApp->getLedgerDB()->getDB();
			db->executeSQL("BEGIN TRANSACTION;");
			for (std::vector<SerializedValidation::pointer>::iterator it = vector.begin(); it != vector.end(); ++it)
				db->executeSQL(boost::str(insVal % (*it)->getLedgerHash().GetHex()
					% (*it)->getSignerPublic().humanNodePublic() % (*it)->getFlags() % (*it)->getCloseTime()
					% db->escape(strCopy((*it)->getSignature()))));
			db->executeSQL("END TRANSACTION;");
		}

		sl.lock();
	}
	mWriting = false;
}
