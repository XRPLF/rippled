#include "PubKeyCache.h"
#include "Application.h"

#include "boost/interprocess/sync/scoped_lock.hpp"


CKey::pointer PubKeyCache::locate(const NewcoinAddress& id)
{
	if(1)
	{ // is it in cache
		boost::mutex::scoped_lock sl(mLock);
		std::map<NewcoinAddress, CKey::pointer>::iterator it(mCache.find(id));
		if(it!=mCache.end()) return it->second;
	}

	std::string sql="SELECT * from PubKeys WHERE ID='";
	sql.append(id.humanAccountID());
	sql.append("';'");
	std::vector<unsigned char> data;
	data.reserve(65); // our public keys are actually 33 bytes
	int pkSize;

	{ // is it in the database
		ScopedLock sl(theApp->getTxnDB()->getDBLock());
		Database* db=theApp->getTxnDB()->getDB();
		if(!db->executeSQL(sql) || !db->startIterRows())
			return CKey::pointer();
		pkSize=db->getBinary("PubKey", &(data.front()), data.size());
		db->endIterRows();
	}
	data.resize(pkSize);
	CKey::pointer ckp(new CKey());
	if(!ckp->SetPubKey(data))
	{
		assert(false); // bad data in DB
		return CKey::pointer();
	}

	{ // put it in cache (okay if we race with another retriever)
		boost::mutex::scoped_lock sl(mLock);
		mCache.insert(std::make_pair(id, ckp));
	}
	return ckp;
}

CKey::pointer PubKeyCache::store(const NewcoinAddress& id, CKey::pointer key)
{ // stored if needed, returns cached copy (possibly the original)
	{
		boost::mutex::scoped_lock sl(mLock);
		std::pair<std::map<NewcoinAddress,CKey::pointer>::iterator, bool> pit(mCache.insert(std::make_pair(id, key)));
		if(!pit.second) // there was an existing key
			return pit.first->second;
	}

	std::vector<unsigned char> pk=key->GetPubKey();
	std::string encodedPK;
	theApp->getTxnDB()->getDB()->escape(&(pk.front()), pk.size(), encodedPK);

	std::string sql="INSERT INTO PubKeys (ID,PubKey) VALUES ('";
	sql+=id.humanAccountID();
	sql+="',";
	sql+=encodedPK;
	sql.append(");");

	ScopedLock dbl(theApp->getTxnDB()->getDBLock());
	theApp->getTxnDB()->getDB()->executeSQL(sql, true);
	return key;
}

void PubKeyCache::clear()
{
	boost::mutex::scoped_lock sl(mLock);
	mCache.empty();
}
// vim:ts=4
