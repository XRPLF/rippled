#include "PubKeyCache.h"
#include "Application.h"

#include "boost/interprocess/sync/scoped_lock.hpp"


CKey::pointer PubKeyCache::locate(const uint160& id)
{
	if(1)
	{ // is it in cache
		boost::mutex::scoped_lock sl(mLock);
		std::map<uint160, CKey::pointer>::iterator it(mCache.find(id));
		if(it!=mCache.end()) return it->second;
	}

	std::string sql="SELECT * from PubKeys WHERE ID='";
	sql.append(id.GetHex());
	sql.append("';'");
	std::vector<unsigned char> data;
	data.reserve(65); // our public keys are actually 33 bytes
	int pkSize;
	if(1)
	{ // is it in the database
		ScopedLock sl(theApp->getDBLock());
		Database* db=theApp->getDB();
		if(!db->executeSQL(sql.c_str())) return CKey::pointer();
		if(!db->getNextRow()) return CKey::pointer();	
		pkSize=db->getBinary("PubKey", &(data.front()), data.size());
	}
	data.resize(pkSize);
	CKey::pointer ckp(new CKey());
	if(!ckp->SetPubKey(data))
	{
		assert(false); // bad data in DB
		return CKey::pointer();
	}
	
	if(1)
	{ // put it in cache (okay if we race with another retriever)
		boost::mutex::scoped_lock sl(mLock);
		mCache.insert(std::make_pair(id, ckp));
	}
	return ckp;
}

CKey::pointer PubKeyCache::store(const uint160& id, CKey::pointer key)
{ // stored if needed, returns cached copy (possibly the original)
	if(1)
	{
		boost::mutex::scoped_lock sl(mLock);
		std::pair<std::map<uint160,CKey::pointer>::iterator, bool> pit(mCache.insert(std::make_pair(id, key)));
		if(!pit.second) // there was an existing key
			return pit.first->second;
	}
	std::string sql="INSERT INTO PubKeys (ID, PubKey) VALUES ('";
	sql+=id.GetHex();
	sql+="',";

	std::vector<unsigned char> pk=key->GetPubKey();
	std::string encodedPK;
	theApp->getDB()->escape(&(pk.front()), pk.size(), encodedPK);
	sql+=encodedPK;
	sql.append(");");
	ScopedLock dbl(theApp->getDBLock());
	theApp->getDB()->executeSQL(sql.c_str());
	return key;
}

void PubKeyCache::clear()
{
	boost::mutex::scoped_lock sl(mLock);
	mCache.empty();
}
