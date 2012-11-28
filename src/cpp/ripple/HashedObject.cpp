
#include "HashedObject.h"

#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>

#include "Serializer.h"
#include "Application.h"
#include "Log.h"

SETUP_LOG();
DECLARE_INSTANCE(HashedObject);

HashedObjectStore::HashedObjectStore(int cacheSize, int cacheAge) :
	mCache("HashedObjectStore", cacheSize, cacheAge), mWritePending(false), mWriteGeneration(0)
{
	mWriteSet.reserve(128);
}


bool HashedObjectStore::store(HashedObjectType type, uint32 index,
	const std::vector<unsigned char>& data, const uint256& hash)
{ // return: false = already in cache, true = added to cache
	if (!theApp->getHashNodeDB())
	{
		cLog(lsTRACE) << "HOS: no db";
		return true;
	}
	if (mCache.touch(hash))
	{
		cLog(lsTRACE) << "HOS: " << hash << " store: incache";
		return false;
	}
	assert(hash == Serializer::getSHA512Half(data));

	HashedObject::pointer object = boost::make_shared<HashedObject>(type, index, data, hash);
	if (!mCache.canonicalize(hash, object))
	{
//		cLog(lsTRACE) << "Queuing write for " << hash;
		boost::mutex::scoped_lock sl(mWriteMutex);
		mWriteSet.push_back(object);
		if (!mWritePending)
		{
			mWritePending = true;
			boost::thread t(boost::bind(&HashedObjectStore::bulkWrite, this));
			t.detach();
		}
	}
//	else
//		cLog(lsTRACE) << "HOS: already had " << hash;
	return true;
}

void HashedObjectStore::waitWrite()
{
	boost::mutex::scoped_lock sl(mWriteMutex);
	int gen = mWriteGeneration;
	while (mWritePending && (mWriteGeneration == gen))
		mWriteCondition.wait(sl);
}

void HashedObjectStore::bulkWrite()
{
	LoadEvent::pointer event = theApp->getJobQueue().getLoadEvent(jtDISK);
	while (1)
	{
		std::vector< boost::shared_ptr<HashedObject> > set;
		set.reserve(128);

		{
			boost::mutex::scoped_lock sl(mWriteMutex);
			mWriteSet.swap(set);
			assert(mWriteSet.empty());
			++mWriteGeneration;
			mWriteCondition.notify_all();
			if (set.empty())
			{
				mWritePending = false;
				return;
			}
		}
//		cLog(lsTRACE) << "HOS: writing " << set.size();

		static boost::format fExists("SELECT ObjType FROM CommittedObjects WHERE Hash = '%s';");
		static boost::format
			fAdd("INSERT INTO CommittedObjects (Hash,ObjType,LedgerIndex,Object) VALUES ('%s','%c','%u',%s);");

		Database* db = theApp->getHashNodeDB()->getDB();
		{
			ScopedLock sl( theApp->getHashNodeDB()->getDBLock());

			db->executeSQL("BEGIN TRANSACTION;");

			BOOST_FOREACH(const boost::shared_ptr<HashedObject>& it, set)
			{
				if (!SQL_EXISTS(db, boost::str(fExists % it->getHash().GetHex())))
				{
					char type;
					switch(it->getType())
					{
						case hotLEDGER:				type = 'L'; break;
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
	}
}

HashedObject::pointer HashedObjectStore::retrieve(const uint256& hash)
{
	HashedObject::pointer obj;
	{
		obj = mCache.fetch(hash);
		if (obj)
		{
			cLog(lsTRACE) << "HOS: " << hash << " fetch: incache";
			return obj;
		}
	}

	if (!theApp || !theApp->getHashNodeDB())
		return HashedObject::pointer();
	std::string sql = "SELECT * FROM CommittedObjects WHERE Hash='";
	sql.append(hash.GetHex());
	sql.append("';");

	std::vector<unsigned char> data;
	{
		ScopedLock sl(theApp->getHashNodeDB()->getDBLock());
		Database* db = theApp->getHashNodeDB()->getDB();

		if (!db->executeSQL(sql) || !db->startIterRows())
		{
//			cLog(lsTRACE) << "HOS: " << hash << " fetch: not in db";
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
