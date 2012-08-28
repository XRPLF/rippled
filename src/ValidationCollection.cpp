
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
		uint32 valClose = val->getCloseTime();
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
			boost::unordered_map<uint160, ValidationPair>::iterator it = mCurrentValidations.find(node);
			if ((it == mCurrentValidations.end()) || (!it->second.newest) ||
				(val->getCloseTime() > it->second.newest->getCloseTime()))
			{
				if (it != mCurrentValidations.end())
				{
					if  (it->second.oldest)
					{
						mStaleValidations.push_back(it->second.oldest);
						condWrite();
					}
					it->second.oldest = it->second.newest;
					it->second.newest = val;
				}
				else
					mCurrentValidations.insert(std::make_pair(node, ValidationPair(val)));
			}
		}
	}

	Log(lsINFO) << "Val for " << hash.GetHex() << " from " << signer.humanNodePublic()
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
	uint32 now = theApp->getOPs().getCloseTimeNC();
	if (it != mValidations.end())
	{
		for (ValidationSet::iterator vit = it->second.begin(), end = it->second.end(); vit != end; ++vit)
		{
			bool isTrusted = vit->second->isTrusted();
			if (isTrusted && currentOnly)
			{
				uint32 closeTime = vit->second->getCloseTime();
				if ((now < (closeTime - LEDGER_EARLY_INTERVAL)) || (now > (closeTime + LEDGER_VAL_INTERVAL)))
					isTrusted = false;
				else
				{
#ifdef VC_DEBUG
					Log(lsINFO) << "VC: Untrusted due to time " << ledger.GetHex();
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
	Log(lsINFO) << "VC: " << ledger.GetHex() << "t:" << trusted << " u:" << untrusted;
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

int ValidationCollection::getCurrentValidationCount(uint32 afterTime)
{
	int count = 0;
	boost::mutex::scoped_lock sl(mValidationLock);
	for (boost::unordered_map<uint160, ValidationPair>::iterator it = mCurrentValidations.begin(),
		end = mCurrentValidations.end(); it != end; ++it)
	{
		if (it->second.newest->isTrusted() && (it->second.newest->getCloseTime() > afterTime))
			++count;
	}
	return count;
}

boost::unordered_map<uint256, int> ValidationCollection::getCurrentValidations()
{
    uint32 now = theApp->getOPs().getCloseTimeNC();
	boost::unordered_map<uint256, int> ret;

	{
		boost::mutex::scoped_lock sl(mValidationLock);
		boost::unordered_map<uint160, ValidationPair>::iterator it = mCurrentValidations.begin();
		while (it != mCurrentValidations.end())
		{
			ValidationPair& pair = it->second;

			if (pair.oldest && (now > (pair.oldest->getCloseTime() + LEDGER_VAL_INTERVAL)))
			{
#ifdef VC_DEBUG
				Log(lsINFO) << "VC: " << it->first.GetHex() << " removeOldestStale";
#endif
				mStaleValidations.push_back(pair.oldest);
				pair.oldest = SerializedValidation::pointer();
				condWrite();
			}
			if (pair.newest && (now > (pair.newest->getCloseTime() + LEDGER_VAL_INTERVAL)))
			{
#ifdef VC_DEBUG
				Log(lsINFO) << "VC: " << it->first.GetHex() << " removeNewestStale";
#endif
				mStaleValidations.push_back(pair.newest);
				pair.newest = SerializedValidation::pointer();
				condWrite();
			}
			if (!pair.newest && !pair.oldest)
				it = mCurrentValidations.erase(it);
			else
			{
				if (pair.oldest)
				{
#ifdef VC_DEBUG
					Log(lsTRACE) << "VC: OLD " << pair.oldest->getLedgerHash().GetHex() << " " <<
						boost::lexical_cast<std::string>(pair.oldest->getCloseTime());
#endif
					++ret[pair.oldest->getLedgerHash()];
				}
				if (pair.newest)
				{
#ifdef VC_DEBUG
					Log(lsTRACE) << "VC: NEW " << pair.newest->getLedgerHash().GetHex() << " " <<
						boost::lexical_cast<std::string>(pair.newest->getCloseTime());
#endif
					++ret[pair.newest->getLedgerHash()];
				}
				++it;
			}
		}
	}

	return ret;
}

bool ValidationCollection::isDeadLedger(const uint256& ledger)
{
	BOOST_FOREACH(const uint256& it, mDeadLedgers)
		if (it == ledger)
			return true;
	return false;
}

void ValidationCollection::addDeadLedger(const uint256& ledger)
{
	if (isDeadLedger(ledger))
		return;

	mDeadLedgers.push_back(ledger);
	if (mDeadLedgers.size() >= 128)
		mDeadLedgers.pop_front();
}

void ValidationCollection::flush()
{
		boost::mutex::scoped_lock sl(mValidationLock);
		boost::unordered_map<uint160, ValidationPair>::iterator it = mCurrentValidations.begin();
		bool anyNew = false;
		while (it != mCurrentValidations.end())
		{
			if (it->second.oldest)
				mStaleValidations.push_back(it->second.oldest);
			if (it->second.newest)
				mStaleValidations.push_back(it->second.newest);
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
		"(LedgerHash,NodePubKey,Flags,CloseTime,Signature) VALUES ('%s','%s','%u','%u',%s);");

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


			BOOST_FOREACH(const SerializedValidation::pointer& it, vector)
				db->executeSQL(boost::str(insVal % it->getLedgerHash().GetHex()
					% it->getSignerPublic().humanNodePublic() % it->getFlags() % it->getCloseTime()
					% db->escape(strCopy(it->getSignature()))));
			db->executeSQL("END TRANSACTION;");
		}

		sl.lock();
	}
	mWriting = false;
}
