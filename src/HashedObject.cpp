
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

// FIXME: Stores should be added to a queue that's services by an auxilliary thread or from an
// auxilliary thread pool. These should be tied into a cache, since you need one to handle
// an immedate read back (before the write completes)

bool HashedObject::store(HashedObjectType type, uint32 index, const std::vector<unsigned char>& data,
	const uint256& hash)
{
	if (!theApp->getHashNodeDB()) return true;
#ifdef DEBUG
	Serializer s(data);
	assert(hash == s.getSHA512Half());
#endif
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

	ScopedLock sl(theApp->getHashNodeDB()->getDBLock());
	Database* db = theApp->getHashNodeDB()->getDB();
	return db->executeSQL(sql);
}

bool HashedObject::store() const
{
#ifdef DEBUG
	assert(checkHash());
#endif
	return store(mType, mLedgerIndex, mData, mHash);
}

HashedObject::pointer HashedObject::retrieve(const uint256& hash)
{
	if (!theApp || !theApp->getHashNodeDB()) return HashedObject::pointer();
	std::string sql = "SELECT * from CommittedObjects WHERE Hash='";
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
	}

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

	HashedObject::pointer obj = boost::make_shared<HashedObject>(htype, index, data);
	obj->mHash = hash;
#ifdef DEBUG
	assert(obj->checkHash());
#endif
	return obj;
}

// vim:ts=4
