
#include "HashedObject.h"

#ifdef USE_LEVELDB
#include "leveldb/db.h"
#endif

#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>

#include "../database/SqliteDatabase.h"

#include "Serializer.h"
#include "Application.h"
#include "Log.h"

SETUP_LOG();
DECLARE_INSTANCE(HashedObject);

HashedObjectStore::HashedObjectStore(int cacheSize, int cacheAge) :
	mCache("HashedObjectStore", cacheSize, cacheAge), mNegativeCache("HashedObjectNegativeCache", 0, 120),
	mWriteGeneration(0), mWritePending(false)
{
	mWriteSet.reserve(128);
}

void HashedObjectStore::tune(int size, int age)
{
	mCache.setTargetSize(size);
	mCache.setTargetAge(age);
}

#ifdef USE_LEVELDB

bool HashedObjectStore::store(HashedObjectType type, uint32 index,
	const std::vector<unsigned char>& data, const uint256& hash)
{ // return: false = already in cache, true = added to cache
	if (!theApp->getHashNodeDB())
	{
		cLog(lsWARNING) << "HOS: no db";
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
		LoadEvent::autoptr event(theApp->getJobQueue().getLoadEventAP(jtHO_WRITE, "HOS::store"));

		std::vector<unsigned char> rawData(9 + data.size());
		unsigned char* bufPtr = &rawData.front();

		*reinterpret_cast<uint32*>(bufPtr + 0) = ntohl(index);
		*reinterpret_cast<uint32*>(bufPtr + 4) = ntohl(index);
		*(bufPtr + 8) = static_cast<unsigned char>(type);
		memcpy(bufPtr + 9, &data.front(), data.size());

		leveldb::Status st = theApp->getHashNodeDB()->Put(leveldb::WriteOptions(),
			leveldb::Slice(reinterpret_cast<const char *>(hash.begin()), hash.size()),
			leveldb::Slice(reinterpret_cast<const char *>(bufPtr), 9 + data.size()));
		if (!st.ok())
		{
			cLog(lsFATAL) << "Failed to store hash node";
			assert(false);
		}
	}
	else
		cLog(lsDEBUG) << "HOS: store race";
	return true;
}

void HashedObjectStore::waitWrite()
{
}

HashedObject::pointer HashedObjectStore::retrieve(const uint256& hash)
{
	HashedObject::pointer obj = mCache.fetch(hash);
	if (obj)
		return obj;

	if (!theApp || !theApp->getHashNodeDB())
	{
		cLog(lsWARNING) << "HOS: no db";
		return obj;
	}

	LoadEvent::autoptr event(theApp->getJobQueue().getLoadEventAP(jtHO_READ, "HOS::retrieve"));
	std::string sData;
	leveldb::Status st = theApp->getHashNodeDB()->Get(leveldb::ReadOptions(),
		leveldb::Slice(reinterpret_cast<const char *>(hash.begin()), hash.size()), &sData);
	if (!st.ok())
	{
		assert(st.IsNotFound());
		return obj;
	}

	const unsigned char* bufPtr = reinterpret_cast<const unsigned char*>(&sData.front());
	uint32 index = htonl(*reinterpret_cast<const uint32*>(bufPtr));
	int htype = bufPtr[8];

	obj = boost::make_shared<HashedObject>(static_cast<HashedObjectType>(htype), index,
		bufPtr + 9, sData.size() - 9, hash);
	mCache.canonicalize(hash, obj);

	cLog(lsTRACE) << "HOS: " << hash << " fetch: in db";
	return obj;
}

#else

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
			theApp->getJobQueue().addJob(jtWRITE, "HashedObject::store",
				BIND_TYPE(&HashedObjectStore::bulkWrite, this));
		}
	}
//	else
//		cLog(lsTRACE) << "HOS: already had " << hash;
	mNegativeCache.del(hash);
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

#ifndef NO_SQLITE3_PREPARE

	{
		Database* db = theApp->getHashNodeDB()->getDB();
		static SqliteStatement pStB(db->getSqliteDB(), "BEGIN TRANSACTION;", !theConfig.RUN_STANDALONE);
		static SqliteStatement pStE(db->getSqliteDB(), "END TRANSACTION;", !theConfig.RUN_STANDALONE);
		static SqliteStatement pSt(db->getSqliteDB(),
			"INSERT OR IGNORE INTO CommittedObjects "
				"(Hash,ObjType,LedgerIndex,Object) VALUES (?, ?, ?, ?);", !theConfig.RUN_STANDALONE);

		pStB.step();
		pStB.reset();

		BOOST_FOREACH(const boost::shared_ptr<HashedObject>& it, set)
		{
			const char* type;

			switch (it->getType())
			{
				case hotLEDGER:				type = "L"; break;
				case hotTRANSACTION:		type = "T"; break;
				case hotACCOUNT_NODE:		type = "A"; break;
				case hotTRANSACTION_NODE:	type = "N"; break;
				default:					type = "U";
			}

			pSt.bind(1, it->getHash().GetHex());
			pSt.bind(2, type);
			pSt.bind(3, it->getIndex());
			pSt.bindStatic(4, it->getData());
			int ret = pSt.step();
			if (!pSt.isDone(ret))
			{
				cLog(lsFATAL) << "Error saving hashed object " << ret;
				assert(false);
			}
			pSt.reset();
		}

		pStE.step();
		pStE.reset();
	}

#else

		static boost::format
			fAdd("INSERT OR IGNORE INTO CommittedObjects "
				"(Hash,ObjType,LedgerIndex,Object) VALUES ('%s','%c','%u',%s);");

		Database* db = theApp->getHashNodeDB()->getDB();
		{
			ScopedLock sl(theApp->getHashNodeDB()->getDBLock());

			db->executeSQL("BEGIN TRANSACTION;");

			BOOST_FOREACH(const boost::shared_ptr<HashedObject>& it, set)
			{
				char type;

				switch (it->getType())
				{
					case hotLEDGER:				type = 'L'; break;
					case hotTRANSACTION:		type = 'T'; break;
					case hotACCOUNT_NODE:		type = 'A'; break;
					case hotTRANSACTION_NODE:	type = 'N'; break;
					default:					type = 'U';
				}
				db->executeSQL(boost::str(boost::format(fAdd)
					% it->getHash().GetHex() % type % it->getIndex() % sqlEscape(it->getData())));
			}

			db->executeSQL("END TRANSACTION;");
		}
#endif

	}
}

HashedObject::pointer HashedObjectStore::retrieve(const uint256& hash)
{
	HashedObject::pointer obj = mCache.fetch(hash);
	if (obj)
		return obj;

	if (mNegativeCache.isPresent(hash))
		return obj;

	if (!theApp || !theApp->getHashNodeDB())
		return obj;

	std::vector<unsigned char> data;
	std::string type;
	uint32 index;

#ifndef NO_SQLITE3_PREPARE
	{
		ScopedLock sl(theApp->getHashNodeDB()->getDBLock());
		static SqliteStatement pSt(theApp->getHashNodeDB()->getDB()->getSqliteDB(),
			"SELECT ObjType,LedgerIndex,Object FROM CommittedObjects WHERE Hash = ?;");
		LoadEvent::autoptr event(theApp->getJobQueue().getLoadEventAP(jtDISK, "HOS::retrieve"));

		pSt.bind(1, hash.GetHex());
		int ret = pSt.step();
		if (pSt.isDone(ret))
		{
			pSt.reset();
			mNegativeCache.add(hash);
			cLog(lsTRACE) << "HOS: " << hash <<" fetch: not in db";
			return obj;
		}

		type = pSt.peekString(0);
		index = pSt.getUInt32(1);
		pSt.getBlob(2).swap(data);
		pSt.reset();
	}

#else

	std::string sql = "SELECT * FROM CommittedObjects WHERE Hash='";
	sql.append(hash.GetHex());
	sql.append("';");


	{
		ScopedLock sl(theApp->getHashNodeDB()->getDBLock());
		Database* db = theApp->getHashNodeDB()->getDB();

		if (!db->executeSQL(sql) || !db->startIterRows())
		{
			sl.unlock();
			mNegativeCache.add(hash);
			return obj;
		}

		db->getStr("ObjType", type);
		index = db->getBigInt("LedgerIndex");

		int size = db->getBinary("Object", NULL, 0);
		data.resize(size);
		db->getBinary("Object", &(data.front()), size);
		db->endIterRows();
	}
#endif

#ifdef PARANOID
	assert(Serializer::getSHA512Half(data) == hash);
#endif

	HashedObjectType htype = hotUNKNOWN;
	switch (type[0])
	{
		case 'L': htype = hotLEDGER; break;
		case 'T': htype = hotTRANSACTION; break;
		case 'A': htype = hotACCOUNT_NODE; break;
		case 'N': htype = hotTRANSACTION_NODE; break;
		default:
		assert(false);
			cLog(lsERROR) << "Invalid hashed object";
			mNegativeCache.add(hash);
			return obj;
	}

	obj = boost::make_shared<HashedObject>(htype, index, data, hash);
	mCache.canonicalize(hash, obj);

	cLog(lsTRACE) << "HOS: " << hash << " fetch: in db";
	return obj;
}

#endif

int HashedObjectStore::import(const std::string& file, bool checkHashes)
{
	cLog(lsWARNING) << "Hashed object import from \"" << file << "\".";
	UPTR_T<Database> importDB(new SqliteDatabase(file.c_str()));
	importDB->connect();

	int count = 0;

	SQL_FOREACH(importDB, "SELECT * FROM CommittedObjects;")
	{
		uint256 hash;
		std::string hashStr;
		importDB->getStr("Hash", hashStr);
		hash.SetHexExact(hashStr);
		if (hash.isZero())
		{
			cLog(lsWARNING) << "zero hash found in import table";
		}
		else
		{
			std::vector<unsigned char> data;
			std::string type;
			importDB->getStr("ObjType", type);
			uint32 index = importDB->getBigInt("LedgerIndex");

			int size = importDB->getBinary("Object", NULL, 0);
			data.resize(size);
			importDB->getBinary("Object", &(data.front()), size);

			assert(Serializer::getSHA512Half(data) == hash);

			HashedObjectType htype = hotUNKNOWN;
			switch (type[0])
			{
				case 'L': htype = hotLEDGER; break;
				case 'T': htype = hotTRANSACTION; break;
				case 'A': htype = hotACCOUNT_NODE; break;
				case 'N': htype = hotTRANSACTION_NODE; break;
				default:
					assert(false);
					cLog(lsERROR) << "Invalid hashed object";
			}

			if (checkHashes && Serializer::getSHA512Half(data) != hash)
			{
				cLog(lsWARNING) << "Hash mismatch in import table " << hash
					<< " " << Serializer::getSHA512Half(data);
			}
			else
			{
				store(htype, index, data, hash);
				++count;
			}
		}
		if ((count % 10000) == 0)
		{
			cLog(lsINFO) << "Import in progress: " << count;
		}
	}

	cLog(lsWARNING) << "Imported " << count << " nodes";
	return count;
}

// vim:ts=4
