
#include "boost/lexical_cast.hpp"

#include "HashedObject.h"
#include "Serializer.h"
#include "Application.h"



bool HashedObject::checkHash() const
{
	uint256 hash=Serializer::getSHA512Half(mData);
	return hash==mHash;
}

bool HashedObject::checkFixHash()
{
	uint256 hash=Serializer::getSHA512Half(mData);
	if(hash==mHash) return true;
	mHash=hash;
	return false;
}

void HashedObject::setHash()
{
	mHash=Serializer::getSHA512Half(mData);
}

/*
CREATE TABLE CommittedObjects (					-- used to synch nodes
        Hash            BLOB PRIMARY KEY,
        ObjType         CHAR(1) NOT NULL,		-- (L)edger, (T)ransaction, (A)ccount node, transaction (N)ode
        LedgerIndex     BIGINT UNSIGNED,		-- 0 if none
        Object          BLOB
);
CREATE INDEX ObjectLocate ON CommittedObjects(LedgerIndex, ObjType);
*/


bool HashedObject::store() const
{
#ifdef DEBUG
	assert(checkHash());
#endif
	std::string sql="INSERT INTO CommitedObjects (Hash,ObjType,LedgerIndex,Object) values (";
	sql.append(mHash.GetHex());
	switch(mType)
	{
		case LEDGER: sql.append(",L,"); break;
		case TRANSACTION: sql.append(",T,"); break;
		case ACCOUNT_NODE: sql.append(",A,"); break;
		case TRANSACTION_NODE: sql.append(",N,"); break;
		default: sql.append(",U,"); break;
	}
	sql.append(boost::lexical_cast<std::string>(mLedgerIndex));
	sql.append(",");

	std::string obj;
	theApp->getDB()->escape(&(mData.front()), mData.size(), obj);
	sql.append(obj);

	ScopedLock sl(theApp->getDBLock());
	Database* db=theApp->getDB();
	return db->executeSQL(sql.c_str());
}

HashedObject::pointer retrieve(const uint256& hash)
{
	std::string sql="SELECT * from CommitedOjects where Hash=";
	sql.append(hash.GetHex());

	std::string type;
	uint32 index;
	std::vector<unsigned char> data;
	data.reserve(8192);
	if(1)
	{
		ScopedLock sl(theApp->getDBLock());
		Database* db=theApp->getDB();

		if(!db->executeSQL(sql.c_str())) return HashedObject::pointer();
		if(!db->getNextRow()) return HashedObject::pointer();

		std::string type;
		db->getStr("ObjType", type);	
		if(type.size()==0) return HashedObject::pointer();
	
		index=db->getBigInt("LedgerIndex");

		int size=db->getBinary("Object", NULL, 0);
		data.resize(size);
		db->getBinary("Object", &(data.front()), size);
	}
	
	HashedObjectType htype=UNKNOWN;
	switch(type[0])
	{
		case 'L': htype=LEDGER; break;
		case 'T': htype=TRANSACTION; break;
		case 'A': htype=ACCOUNT_NODE; break;
		case 'N': htype=TRANSACTION_NODE; break;
	}
	
	HashedObject::pointer obj(new HashedObject(htype, index, data));
	obj->mHash=hash;
#ifdef DEBUG
	assert(obj->checkHash());
#endif
	return obj;
}
