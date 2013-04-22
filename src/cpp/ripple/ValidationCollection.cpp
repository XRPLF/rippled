
#include "ValidationCollection.h"

#include <boost/foreach.hpp>

#include "Application.h"
#include "LedgerTiming.h"
#include "Log.h"

SETUP_LOG();

typedef std::map<uint160, SerializedValidation::pointer>::value_type u160_val_pair;
typedef boost::shared_ptr<ValidationSet> VSpointer;

void ValidationCollection::tune(int size, int age)
{
	mValidations.setTargetSize(size);
	mValidations.setTargetAge(age);
}

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

bool ValidationCollection::addValidation(SerializedValidation::ref val, const std::string& source)
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
		cLog(lsDEBUG) << "Node " << signer.humanNodePublic() << " not in UNL st=" << val->getSignTime() <<
			", hash=" << val->getLedgerHash() << ", shash=" << val->getSigningHash() << " src=" << source;
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
				mCurrentValidations.emplace(node, val);
			else if (!it->second)
				it->second = val;
			else if (val->getSignTime() > it->second->getSignTime())
			{
				val->setPreviousHash(it->second->getLedgerHash());
				mStaleValidations.push_back(it->second);
				it->second = val;
				condWrite();
			}
			else
				isCurrent = false;
		}
	}

	cLog(lsDEBUG) << "Val for " << hash << " from " << signer.humanNodePublic()
		<< " added " << (val->isTrusted() ? "trusted/" : "UNtrusted/") << (isCurrent ? "current" : "stale");
	if (val->isTrusted())
		theApp->getLedgerMaster().checkAccept(hash);
	return isCurrent;
}

ValidationSet ValidationCollection::getValidations(const uint256& ledger)
{
	{
		boost::mutex::scoped_lock sl(mValidationLock);
		VSpointer set = findSet(ledger);
		if (set)
			return ValidationSet(*set);
	}
	return ValidationSet();
}

void ValidationCollection::getValidationCount(const uint256& ledger, bool currentOnly, int& trusted, int &untrusted)
{
	trusted = untrusted = 0;
	boost::mutex::scoped_lock sl(mValidationLock);
	VSpointer set = findSet(ledger);
	if (set)
	{
		uint32 now = theApp->getOPs().getNetworkTimeNC();
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
ValidationCollection::getCurrentValidations(uint256 currentLedger, uint256 priorLedger)
{
    uint32 cutoff = theApp->getOPs().getNetworkTimeNC() - LEDGER_VAL_INTERVAL;
    bool valCurrentLedger = currentLedger.isNonZero();
    bool valPriorLedger = priorLedger.isNonZero();

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
			bool countPreferred = (valCurrentLedger && it->second->isPreviousHash(currentLedger)) ||
				(valPriorLedger && (it->second->getLedgerHash() == priorLedger));
			tLog(countPreferred, lsDEBUG) << "Counting for " << currentLedger << " not " << it->second->getLedgerHash();
			currentValidationCount& p = countPreferred ? ret[currentLedger] : ret[it->second->getLedgerHash()];

			++(p.first);
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

	cLog(lsINFO) << "Flushing validations";
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
	cLog(lsDEBUG) << "Validations flushed";
}

void ValidationCollection::condWrite()
{
	if (mWriting)
		return;
	mWriting = true;
	theApp->getJobQueue().addJob(jtWRITE, "ValidationCollection::doWrite",
		BIND_TYPE(&ValidationCollection::doWrite, this, P_1));
}

void ValidationCollection::doWrite(Job&)
{
	LoadEvent::autoptr event(theApp->getJobQueue().getLoadEventAP(jtDISK, "ValidationWrite"));
	boost::format insVal("INSERT INTO Validations "
		"(LedgerHash,NodePubKey,SignTime,RawData) VALUES ('%s','%s','%u',%s);");

	boost::mutex::scoped_lock sl(mValidationLock);
	assert(mWriting);
	while (!mStaleValidations.empty())
	{
		std::vector<SerializedValidation::pointer> vector;
		vector.reserve(512);
		mStaleValidations.swap(vector);
		sl.unlock();
		{
			Database *db = theApp->getLedgerDB()->getDB();
			ScopedLock dbl(theApp->getLedgerDB()->getDBLock());

			Serializer s(1024);
			db->executeSQL("BEGIN TRANSACTION;");
			BOOST_FOREACH(SerializedValidation::ref it, vector)
			{
				s.erase();
				it->add(s);
				db->executeSQL(boost::str(insVal % it->getLedgerHash().GetHex()
					% it->getSignerPublic().humanNodePublic() % it->getSignTime()
					% sqlEscape(s.peekData())));
			}
			db->executeSQL("END TRANSACTION;");
		}
		sl.lock();
	}
	mWriting = false;
}

void ValidationCollection::sweep()
{
	boost::mutex::scoped_lock sl(mValidationLock);
	mValidations.sweep();
}

// vim:ts=4
