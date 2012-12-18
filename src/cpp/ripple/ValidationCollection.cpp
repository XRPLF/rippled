
#include "ValidationCollection.h"

#include <boost/foreach.hpp>

#include "Application.h"
#include "LedgerTiming.h"
#include "Log.h"

SETUP_LOG();

typedef std::map<uint160, SerializedValidation::pointer>::value_type u160_val_pair;
typedef boost::shared_ptr<ValidationSet> VSpointer;

VSpointer ValidationCollection::findCreateSet(const uint256& ledgerHash)
{
	VSpointer j = mValidations.fetch(ledgerHash);
	if (!j)
	{
		j = boost::make_shared<ValidationSet>();
		mValidations.canonicalize(ledgerHash, j);
	}
	return j;
}

VSpointer ValidationCollection::findSet(const uint256& ledgerHash)
{
	return mValidations.fetch(ledgerHash);
}

bool ValidationCollection::addValidation(const SerializedValidation::pointer& val)
{
	RippleAddress signer = val->getSignerPublic();
	bool isCurrent = false;
	if (theApp->getUNL().nodeInUNL(signer) || val->isTrusted())
	{
		val->setTrusted();
		uint32 now = theApp->getOPs().getCloseTimeNC();
		uint32 valClose = val->getSignTime();
		if ((now > (valClose - LEDGER_EARLY_INTERVAL)) && (now < (valClose + LEDGER_VAL_INTERVAL)))
			isCurrent = true;
		else
		{
			cLog(lsWARNING) << "Received stale validation now=" << now << ", close=" << valClose;
		}
	}
	else
	{
		cLog(lsINFO) << "Node " << signer.humanNodePublic() << " not in UNL";
	}

	uint256 hash = val->getLedgerHash();
	uint160 node = signer.getNodeID();

	{
		boost::mutex::scoped_lock sl(mValidationLock);
		if (!findCreateSet(hash)->insert(std::make_pair(node, val)).second)
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

	cLog(lsINFO) << "Val for " << hash << " from " << signer.humanNodePublic()
		<< " added " << (val->isTrusted() ? "trusted/" : "UNtrusted/") << (isCurrent ? "current" : "stale");
	return isCurrent;
}

ValidationSet ValidationCollection::getValidations(const uint256& ledger)
{
	{
		boost::mutex::scoped_lock sl(mValidationLock);
		VSpointer set = findSet(ledger);
		if (set != VSpointer())
			return *set;
	}
	return ValidationSet();
}

void ValidationCollection::getValidationCount(const uint256& ledger, bool currentOnly, int& trusted, int &untrusted)
{
	trusted = untrusted = 0;
	boost::mutex::scoped_lock sl(mValidationLock);
	VSpointer set = findSet(ledger);
	uint32 now = theApp->getOPs().getNetworkTimeNC();
	if (set)
	{
		BOOST_FOREACH(u160_val_pair& it, *set)
		{
			bool isTrusted = it.second->isTrusted();
			if (isTrusted && currentOnly)
			{
				uint32 closeTime = it.second->getSignTime();
				if ((now < (closeTime - LEDGER_EARLY_INTERVAL)) || (now > (closeTime + LEDGER_VAL_INTERVAL)))
					isTrusted = false;
				else
				{
					cLog(lsTRACE) << "VC: Untrusted due to time " << ledger;
				}
			}
			if (isTrusted)
				++trusted;
			else
				++untrusted;
		}
	}
	cLog(lsTRACE) << "VC: " << ledger << "t:" << trusted << " u:" << untrusted;
}

void ValidationCollection::getValidationTypes(const uint256& ledger, int& full, int& partial)
{
	full = partial = 0;
	boost::mutex::scoped_lock sl(mValidationLock);
	VSpointer set = findSet(ledger);
	if (set)
	{
		BOOST_FOREACH(u160_val_pair& it, *set)
		{
			if (it.second->isTrusted())
			{
				if (it.second->isFull())
					++full;
				else
					++partial;
			}
		}
	}
	cLog(lsTRACE) << "VC: " << ledger << "f:" << full << " p:" << partial;
}


int ValidationCollection::getTrustedValidationCount(const uint256& ledger)
{
	int trusted = 0;
	boost::mutex::scoped_lock sl(mValidationLock);
	VSpointer set = findSet(ledger);
	if (set)
	{
		BOOST_FOREACH(u160_val_pair& it, *set)
		{
			if (it.second->isTrusted())
				++trusted;
		}
	}
	return trusted;
}

int ValidationCollection::getNodesAfter(const uint256& ledger)
{ // Number of trusted nodes that have moved past this ledger
	int count = 0;
	boost::mutex::scoped_lock sl(mValidationLock);
	BOOST_FOREACH(u160_val_pair& it, mCurrentValidations)
	{
		if (it.second->isTrusted() && it.second->isPreviousHash(ledger))
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
		BOOST_FOREACH(u160_val_pair& it, mCurrentValidations)
		{
			if (it.second->isTrusted())
			{
				if (it.second->isFull())
					++goodNodes;
				else
					++badNodes;
			}
		}
	}
	return (goodNodes * 100) / (goodNodes + badNodes);
}

std::list<SerializedValidation::pointer> ValidationCollection::getCurrentTrustedValidations()
{
    uint32 cutoff = theApp->getOPs().getNetworkTimeNC() - LEDGER_VAL_INTERVAL;

	std::list<SerializedValidation::pointer> ret;

	boost::mutex::scoped_lock sl(mValidationLock);
	boost::unordered_map<uint160, SerializedValidation::pointer>::iterator it = mCurrentValidations.begin();
	while (it != mCurrentValidations.end())
	{
		if (!it->second) // contains no record
			it = mCurrentValidations.erase(it);
		else if (it->second->getSignTime() < cutoff)
		{ // contains a stale record
			mStaleValidations.push_back(it->second);
			it->second.reset();
			condWrite();
			it = mCurrentValidations.erase(it);
		}
		else
		{ // contains a live record
			if (it->second->isTrusted())
				ret.push_back(it->second);
			++it;
		}
	}

	return ret;
}

boost::unordered_map<uint256, currentValidationCount>
ValidationCollection::getCurrentValidations(uint256 currentLedger)
{
    uint32 cutoff = theApp->getOPs().getNetworkTimeNC() - LEDGER_VAL_INTERVAL;
    bool valCurrentLedger = currentLedger.isNonZero();

	boost::unordered_map<uint256, currentValidationCount> ret;

	boost::mutex::scoped_lock sl(mValidationLock);
	boost::unordered_map<uint160, SerializedValidation::pointer>::iterator it = mCurrentValidations.begin();
	while (it != mCurrentValidations.end())
	{
		if (!it->second) // contains no record
			it = mCurrentValidations.erase(it);
		else if (it->second->getSignTime() < cutoff)
		{ // contains a stale record
			mStaleValidations.push_back(it->second);
			it->second.reset();
			condWrite();
			it = mCurrentValidations.erase(it);
		}
		else
		{ // contains a live record
			bool countPreferred = valCurrentLedger && it->second->isPreviousHash(currentLedger);
			currentValidationCount& p = countPreferred ? ret[currentLedger] : ret[it->second->getLedgerHash()];

			++(p.first); // count for the favored ledger
			uint160 ni = it->second->getNodeID();
			if (ni > p.second)
				p.second = ni;
			++it;
		}
	}

	return ret;
}

void ValidationCollection::flush()
{
		bool anyNew = false;

		boost::mutex::scoped_lock sl(mValidationLock);
		BOOST_FOREACH(u160_val_pair& it, mCurrentValidations)
		{
			if (it.second)
				mStaleValidations.push_back(it.second);
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
	LoadEvent::autoptr event(theApp->getJobQueue().getLoadEventAP(jtDISK));
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
