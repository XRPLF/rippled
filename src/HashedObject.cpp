
#include "HashedObject.h"

#include <boost/lexical_cast.hpp>

#include "Serializer.h"
#include "Application.h"
#include "Log.h"

bool HashedObject::checkHash() const
{
	uint256 hash = Serializer::getSHA512Half(mData);
	return hash == mHash;
}

bool HashedObject::checkFixHash()
{
	uint256 hash = Serializer::getSHA512Half(mData);
	if (hash == mHash) return true;
	mHash = hash;
	return false;
}

void HashedObject::setHash()
{
	mHash = Serializer::getSHA512Half(mData);
}

HashedObjectStore::HashedObjectStore(int cacheSize, int cacheAge) :
	mCache(cacheSize, cacheAge), mWritePending(false)
{
	mWriteSet.reserve(128);
}


bool HashedObjectStore::store(HashedObjectType type, uint32 index,
	const std::vector<unsigned char>& data, const uint256& hash)
{ // return: false=already in cache, true = added to cache
	if (!theApp->getHashNodeDB()) return true;
	if (mCache.touch(hash)) return false;

	HashedObject::pointer object = boost::make_shared<HashedObject>(type, index, data);
	object->setHash();
	if (object->getHash() != hash)
		throw std::runtime_error("Object added to store doesn't have valid hash");

	boost::recursive_mutex::scoped_lock sl(mWriteMutex);
	mWriteSet.push_back(object);
	if (mWriteSet.size() == 64)
	{
		boost::recursive_mutex::scoped_lock sl(mWriteMutex);
		if (!mWritePending)
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

	boost::format fExists("SELECT ObjType FROM CommittedObjects WHERE Hash = '%s';");
	boost::format fAdd("INSERT INTO ComittedObject (Hash,ObjType,LedgerIndex,Object) VALUES ('%s','%c','%u','%s');");

	Database* db = theApp->getHashNodeDB()->getDB();
	ScopedLock sl = theApp->getHashNodeDB()->getDBLock();

	db->executeSQL("BEGIN TRANSACTION;");

	for (std::vector< boost::shared_ptr<HashedObject> >::iterator it = set.begin(), end = set.end(); it != end; ++it)
	{
		HashedObject& obj = **it;
		if (!SQL_EXISTS(db, boost::str(fExists % obj.getHash().GetHex())))
		{
			char type;
			switch(obj.getType())
			{
				case LEDGER: type = 'L'; break;
				case TRANSACTION: type = 'T'; break;
				case ACCOUNT_NODE: type = 'A'; break;
				case TRANSACTION_NODE: type = 'N'; break;
				default: type = 'U';
			}
			std::string rawData;
			db->escape(&(obj.getData().front()), obj.getData().size(), rawData);
			db->executeSQL(boost::str(fAdd % obj.getHash().GetHex() % type % obj.getIndex() % rawData ));
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
			Log(lsTRACE) << "HOS: " << hash.GetHex() << " fetch: incache";
			return obj;
		}
	}

	if (!theApp || !theApp->getHashNodeDB()) return HashedObject::pointer();
	std::string sql = "SELECT * FROM CommittedObjects WHERE Hash='";
	sql.append(hash.GetHex());
	sql.append("';");

	std::string type;
	uint32 index;
	std::vector<unsigned char> data;
	{
		ScopedLock sl(theApp->getHashNodeDB()->getDBLock());
		Database* db = theApp->getHashNodeDB()->getDB();

		if (!db->executeSQL(sql) || !db->startIterRows())
		{
			Log(lsTRACE) << "HOS: " << hash.GetHex() << " fetch: not in db";
			return HashedObject::pointer();
		}

		std::string type;
		db->getStr("ObjType", type);
		if (type.size() == 0) return HashedObject::pointer();

		index = db->getBigInt("LedgerIndex");

		int size = db->getBinary("Object", NULL, 0);
		data.resize(size);
		db->getBinary("Object", &(data.front()), size);
		db->endIterRows();

		HashedObjectType htype = UNKNOWN;
		switch(type[0])
		{
			case 'L': htype = LEDGER; break;
			case 'T': htype = TRANSACTION; break;
			case 'A': htype = ACCOUNT_NODE; break;
			case 'N': htype = TRANSACTION_NODE; break;
			default:
				Log(lsERROR) << "Invalid hashed object";
				return HashedObject::pointer();
		}

		obj = boost::make_shared<HashedObject>(htype, index, data);
		obj->mHash = hash;
		mCache.canonicalize(hash, obj);
	}
#ifdef DEBUG
	assert(obj->checkHash());
#endif
	Log(lsTRACE) << "HOS: " << hash.GetHex() << " fetch: in db";
	return obj;
}

// vim:ts=4
