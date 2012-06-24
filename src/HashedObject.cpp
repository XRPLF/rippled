
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

// FIXME: Stores should be added to a queue that's serviced by an auxilliary thread or from an
// auxilliary thread pool. These should be tied into a cache, since you need one to handle
// an immedate read back (before the write completes)

bool HashedObjectStore::store(HashedObjectType type, uint32 index,
	const std::vector<unsigned char>& data, const uint256& hash)
{
	if (!theApp->getHashNodeDB()) return true;
	HashedObject::pointer object = boost::make_shared<HashedObject>(type, index, data);
	object->setHash();
	if (object->getHash() != hash)
		throw std::runtime_error("Object added to store doesn't have valid hash");

	std::string sql = "INSERT INTO CommittedObjects (Hash,ObjType,LedgerIndex,Object) VALUES ('";
	sql.append(hash.GetHex());
	switch(type)
	{
		case LEDGER: sql.append("','L','"); break;
		case TRANSACTION: sql.append("','T','"); break;
		case ACCOUNT_NODE: sql.append("','A','"); break;
		case TRANSACTION_NODE: sql.append("','N','"); break;
		default: sql.append("','U','"); break;
	}
	sql.append(boost::lexical_cast<std::string>(index));
	sql.append("',");
	std::string obj;
	theApp->getHashNodeDB()->getDB()->escape(&(data.front()), data.size(), obj);
	sql.append(obj);
	sql.append(");");

	std::string exists =
		boost::str(boost::format("SELECT ObjType FROM CommittedObjects WHERE Hash = '%s';") % hash.GetHex());

	ScopedLock sl(theApp->getHashNodeDB()->getDBLock());
	if (mCache.canonicalize(hash, object))
		return false;
	Database* db = theApp->getHashNodeDB()->getDB();
	if (SQL_EXISTS(db, exists))
		return false;
	return db->executeSQL(sql);
}

HashedObject::pointer HashedObjectStore::retrieve(const uint256& hash)
{
	HashedObject::pointer obj;
	{
		ScopedLock sl(theApp->getHashNodeDB()->getDBLock());
		obj = mCache.fetch(hash);
		if (obj) return obj;
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
			return HashedObject::pointer();

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
	return obj;
}

ScopedLock HashedObjectStore::beginBulk()
{
	ScopedLock sl(theApp->getHashNodeDB()->getDBLock());
	theApp->getHashNodeDB()->getDB()->executeSQL("BEGIN TRANSACTION;");
	return sl;
}

void HashedObjectStore::endBulk()
{
	theApp->getHashNodeDB()->getDB()->executeSQL("END TRANSACTION;");
}

// vim:ts=4
