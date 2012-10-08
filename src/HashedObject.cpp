
#include "HashedObject.h"

#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>

#include "Serializer.h"
#include "Application.h"
#include "Log.h"

SETUP_LOG();

HashedObjectStore::HashedObjectStore(int cacheSize, int cacheAge) :
	mCache(cacheSize, cacheAge), mWritePending(false)
{
	mWriteSet.reserve(128);
}


bool HashedObjectStore::store(HashedObjectType type, uint32 index,
	const std::vector<unsigned char>& data, const uint256& hash)
{ // return: false=already in cache, true = added to cache
	assert(hash == Serializer::getSHA512Half(data));
	if (!theApp->getHashNodeDB()) return true;
	if (mCache.touch(hash))
	{
		cLog(lsTRACE) << "HOS: " << hash << " store: incache";
		return false;
	}

	HashedObject::pointer object = boost::make_shared<HashedObject>(type, index, data, hash);
	if (!mCache.canonicalize(hash, object))
	{
		boost::recursive_mutex::scoped_lock sl(mWriteMutex);
		mWriteSet.push_back(object);
		if (!mWritePending && (mWriteSet.size() >= 64))
		{
			mWritePending = true;
			boost::thread t(boost::bind(&HashedObjectStore::bulkWrite, this));
			t.detach();
		}
	}
	return true;
}

void HashedObjectStore::bulkWrite()
{
	std::vector< boost::shared_ptr<HashedObject> > set;
	set.reserve(128);

	{
		boost::recursive_mutex::scoped_lock sl(mWriteMutex);
		mWriteSet.swap(set);
		mWritePending = false;
	}
	cLog(lsINFO) << "HOS: BulkWrite " << set.size();

	static boost::format fExists("SELECT ObjType FROM CommittedObjects WHERE Hash = '%s';");
	static boost::format
		fAdd("INSERT INTO CommittedObjects (Hash,ObjType,LedgerIndex,Object) VALUES ('%s','%c','%u',%s);");

	Database* db = theApp->getHashNodeDB()->getDB();
	ScopedLock sl = theApp->getHashNodeDB()->getDBLock();

	db->executeSQL("BEGIN TRANSACTION;");

	BOOST_FOREACH(const boost::shared_ptr<HashedObject>& it, set)
	{
		if (!SQL_EXISTS(db, boost::str(fExists % it->getHash().GetHex())))
		{
			char type;
			switch(it->getType())
			{
				case hotLEDGER:				type= 'L'; break;
				case hotTRANSACTION:		type = 'T'; break;
				case hotACCOUNT_NODE:		type = 'A'; break;
				case hotTRANSACTION_NODE:	type = 'N'; break;
				default:					type = 'U';
			}
			std::string rawData;
			db->escape(&(it->getData().front()), it->getData().size(), rawData);
			db->executeSQL(boost::str(fAdd % it->getHash().GetHex() % type % it->getIndex() % rawData ));
		}
	}

	db->executeSQL("END TRANSACTION;");
}

HashedObject::pointer HashedObjectStore::retrieve(const uint256& hash)
{
	HashedObject::pointer obj;
	{
		ScopedLock sl(theApp->getHashNodeDB()->getDBLock());
		obj = mCache.fetch(hash);
		if (obj)
		{
			cLog(lsTRACE) << "HOS: " << hash << " fetch: incache";
			return obj;
		}
	}

	if (!theApp || !theApp->getHashNodeDB()) return HashedObject::pointer();
	std::string sql = "SELECT * FROM CommittedObjects WHERE Hash='";
	sql.append(hash.GetHex());
	sql.append("';");

	std::vector<unsigned char> data;
	{
		ScopedLock sl(theApp->getHashNodeDB()->getDBLock());
		Database* db = theApp->getHashNodeDB()->getDB();

		if (!db->executeSQL(sql) || !db->startIterRows())
		{
			cLog(lsTRACE) << "HOS: " << hash << " fetch: not in db";
			return HashedObject::pointer();
		}

		std::string type;
		db->getStr("ObjType", type);
		if (type.size() == 0) return HashedObject::pointer();

		uint32 index = db->getBigInt("LedgerIndex");

		int size = db->getBinary("Object", NULL, 0);
		data.resize(size);
		db->getBinary("Object", &(data.front()), size);
		db->endIterRows();

		assert(Serializer::getSHA512Half(data) == hash);

		HashedObjectType htype = hotUNKNOWN;
		switch (type[0])
		{
			case 'L': htype = hotLEDGER; break;
			case 'T': htype = hotTRANSACTION; break;
			case 'A': htype = hotACCOUNT_NODE; break;
			case 'N': htype = hotTRANSACTION_NODE; break;
			default:
				cLog(lsERROR) << "Invalid hashed object";
				return HashedObject::pointer();
		}

		obj = boost::make_shared<HashedObject>(htype, index, data, hash);
		mCache.canonicalize(hash, obj);
	}
	cLog(lsTRACE) << "HOS: " << hash << " fetch: in db";
	return obj;
}

// vim:ts=4
