
/*
#include "HashedObject.h"
#include "leveldb/db.h"
#include "leveldb/write_batch.h"
#include "../database/SqliteDatabase.h"
#include "Application.h"
*/

HashedObjectStore::HashedObjectStore(int cacheSize, int cacheAge) :
	mCache("HashedObjectStore", cacheSize, cacheAge), mNegativeCache("HashedObjectNegativeCache", 0, 120),
	mWriteGeneration(0), mWriteLoad(0), mWritePending(false), mLevelDB(false), mEphemeralDB(false)
{
	mWriteSet.reserve(128);

	if (theConfig.NODE_DB == "leveldb" || theConfig.NODE_DB == "LevelDB")
		mLevelDB = true;
	else if (theConfig.NODE_DB == "SQLite" || theConfig.NODE_DB == "sqlite")
		mLevelDB = false;
	else
	{
		WriteLog (lsFATAL, HashedObject) << "Incorrect database selection";
		assert(false);
	}
	if (!theConfig.LDB_EPHEMERAL.empty())
		mEphemeralDB = true;
}

void HashedObjectStore::tune(int size, int age)
{
	mCache.setTargetSize(size);
	mCache.setTargetAge(age);
}

void HashedObjectStore::waitWrite()
{
	boost::mutex::scoped_lock sl(mWriteMutex);
	int gen = mWriteGeneration;
	while (mWritePending && (mWriteGeneration == gen))
		mWriteCondition.wait(sl);
}

int HashedObjectStore::getWriteLoad()
{
	boost::mutex::scoped_lock sl(mWriteMutex);
	return std::max(mWriteLoad, static_cast<int>(mWriteSet.size()));
}

// low-level retrieve
HashedObject::pointer HashedObjectStore::LLRetrieve(const uint256& hash, leveldb::DB* db)
{
	std::string sData;

	leveldb::Status st = db->Get(leveldb::ReadOptions(),
		leveldb::Slice(reinterpret_cast<const char *>(hash.begin()), hash.size()), &sData);
	if (!st.ok())
	{
		assert(st.IsNotFound());
		return HashedObject::pointer();
	}

	const unsigned char* bufPtr = reinterpret_cast<const unsigned char*>(&sData[0]);
	uint32 index = htonl(*reinterpret_cast<const uint32*>(bufPtr));
	int htype = bufPtr[8];

	return boost::make_shared<HashedObject>(static_cast<HashedObjectType>(htype), index,
		bufPtr + 9, sData.size() - 9, hash);
}

// low-level write single
void HashedObjectStore::LLWrite(boost::shared_ptr<HashedObject> ptr, leveldb::DB* db)
{
	HashedObject& obj = *ptr;
	Blob rawData(9 + obj.getData ().size());
	unsigned char* bufPtr = &rawData.front();

	*reinterpret_cast<uint32*>(bufPtr + 0) = ntohl(obj.getIndex ());
	*reinterpret_cast<uint32*>(bufPtr + 4) = ntohl(obj.getIndex ());
	*(bufPtr + 8) = static_cast<unsigned char>(obj.getType ());
	memcpy(bufPtr + 9, &obj.getData ().front(), obj.getData ().size());

	leveldb::Status st = db->Put(leveldb::WriteOptions(),
		leveldb::Slice(reinterpret_cast<const char *>(obj.getHash ().begin()), obj.getHash ().size()),
		leveldb::Slice(reinterpret_cast<const char *>(bufPtr), rawData.size()));
	if (!st.ok())
	{
		WriteLog (lsFATAL, HashedObject) << "Failed to store hash node";
		assert(false);
	}
}

// low-level write set
void HashedObjectStore::LLWrite(const std::vector< boost::shared_ptr<HashedObject> >& set, leveldb::DB* db)
{
	leveldb::WriteBatch batch;

	BOOST_FOREACH(const boost::shared_ptr<HashedObject>& it, set)
	{
		const HashedObject& obj = *it;
		Blob rawData(9 + obj.getData ().size());
		unsigned char* bufPtr = &rawData.front();

		*reinterpret_cast<uint32*>(bufPtr + 0) = ntohl(obj.getIndex ());
		*reinterpret_cast<uint32*>(bufPtr + 4) = ntohl(obj.getIndex ());
		*(bufPtr + 8) = static_cast<unsigned char>(obj.getType ());
		memcpy(bufPtr + 9, &obj.getData ().front(), obj.getData ().size());

		batch.Put(leveldb::Slice(reinterpret_cast<const char *>(obj.getHash ().begin()), obj.getHash ().size()),
			leveldb::Slice(reinterpret_cast<const char *>(bufPtr), rawData.size()));
	}

	leveldb::Status st = db->Write(leveldb::WriteOptions(), &batch);
	if (!st.ok())
	{
		WriteLog (lsFATAL, HashedObject) << "Failed to store hash node";
		assert(false);
	}
}

bool HashedObjectStore::storeLevelDB(HashedObjectType type, uint32 index,
	Blob const& data, const uint256& hash)
{ // return: false = already in cache, true = added to cache
	if (!theApp->getHashNodeLDB())
		return true;

	if (mCache.touch(hash))
		return false;

#ifdef PARANOID
	assert(hash == Serializer::getSHA512Half(data));
#endif

	HashedObject::pointer object = boost::make_shared<HashedObject>(type, index, data, hash);
	if (!mCache.canonicalize(hash, object))
	{
		boost::mutex::scoped_lock sl(mWriteMutex);
		mWriteSet.push_back(object);
		if (!mWritePending)
		{
			mWritePending = true;
			theApp->getJobQueue().addJob(jtWRITE, "HashedObject::store",
				BIND_TYPE(&HashedObjectStore::bulkWriteLevelDB, this, P_1));
		}
	}
	mNegativeCache.del(hash);
	return true;
}

void HashedObjectStore::bulkWriteLevelDB(Job &)
{
	assert(mLevelDB);
	int setSize = 0;
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
				mWriteLoad = 0;
				return;
			}
			mWriteLoad = std::max(setSize, static_cast<int>(mWriteSet.size()));
			setSize = set.size();
		}

		LLWrite(set, theApp->getHashNodeLDB());
		if (mEphemeralDB)
			LLWrite(set, theApp->getEphemeralLDB());
	}
}

HashedObject::pointer HashedObjectStore::retrieveLevelDB(const uint256& hash)
{
	HashedObject::pointer obj = mCache.fetch(hash);
	if (obj || mNegativeCache.isPresent(hash) || !theApp || !theApp->getHashNodeLDB())
		return obj;

	if (mEphemeralDB)
	{
		obj = LLRetrieve(hash, theApp->getEphemeralLDB());
		if (obj)
		{
			mCache.canonicalize(hash, obj);
			return obj;
		}
	}

	{
		LoadEvent::autoptr event(theApp->getJobQueue().getLoadEventAP(jtHO_READ, "HOS::retrieve"));
		obj = LLRetrieve(hash, theApp->getHashNodeLDB());
		if (!obj)
		{
			mNegativeCache.add(hash);
			return obj;
		}
	}

	mCache.canonicalize(hash, obj);

	if (mEphemeralDB)
		LLWrite(obj, theApp->getEphemeralLDB());

	WriteLog (lsTRACE, HashedObject) << "HOS: " << hash << " fetch: in db";
	return obj;
}

bool HashedObjectStore::storeSQLite(HashedObjectType type, uint32 index,
	Blob const& data, const uint256& hash)
{ // return: false = already in cache, true = added to cache
	if (!theApp->getHashNodeDB())
	{
		WriteLog (lsTRACE, HashedObject) << "HOS: no db";
		return true;
	}
	if (mCache.touch(hash))
	{
		WriteLog (lsTRACE, HashedObject) << "HOS: " << hash << " store: incache";
		return false;
	}
	assert(hash == Serializer::getSHA512Half(data));

	HashedObject::pointer object = boost::make_shared<HashedObject>(type, index, data, hash);
	if (!mCache.canonicalize(hash, object))
	{
//		WriteLog (lsTRACE, HashedObject) << "Queuing write for " << hash;
		boost::mutex::scoped_lock sl(mWriteMutex);
		mWriteSet.push_back(object);
		if (!mWritePending)
		{
			mWritePending = true;
			theApp->getJobQueue().addJob(jtWRITE, "HashedObject::store",
				BIND_TYPE(&HashedObjectStore::bulkWriteSQLite, this, P_1));
		}
	}
//	else
//		WriteLog (lsTRACE, HashedObject) << "HOS: already had " << hash;
	mNegativeCache.del(hash);

	return true;
}

void HashedObjectStore::bulkWriteSQLite(Job&)
{
	assert(!mLevelDB);
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
//		WriteLog (lsTRACE, HashedObject) << "HOS: writing " << set.size();

#ifndef NO_SQLITE3_PREPARE

		if (mEphemeralDB)
			LLWrite(set, theApp->getEphemeralLDB());

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
				WriteLog (lsFATAL, HashedObject) << "Error saving hashed object " << ret;
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

HashedObject::pointer HashedObjectStore::retrieveSQLite(const uint256& hash)
{
	HashedObject::pointer obj = mCache.fetch(hash);
	if (obj)
		return obj;

	if (mNegativeCache.isPresent(hash))
		return obj;

	if (mEphemeralDB)
	{
		obj = LLRetrieve(hash, theApp->getEphemeralLDB());
		if (obj)
		{
			mCache.canonicalize(hash, obj);
			return obj;
		}
	}

	if (!theApp || !theApp->getHashNodeDB())
		return obj;

	Blob data;
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
			WriteLog (lsTRACE, HashedObject) << "HOS: " << hash <<" fetch: not in db";
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
			WriteLog (lsERROR, HashedObject) << "Invalid hashed object";
			mNegativeCache.add(hash);
			return obj;
	}

	obj = boost::make_shared<HashedObject>(htype, index, data, hash);
	mCache.canonicalize(hash, obj);

	if (mEphemeralDB)
		LLWrite(obj, theApp->getEphemeralLDB());

	WriteLog (lsTRACE, HashedObject) << "HOS: " << hash << " fetch: in db";
	return obj;
}

int HashedObjectStore::import(const std::string& file)
{
	WriteLog (lsWARNING, HashedObject) << "Hashed object import from \"" << file << "\".";
	UPTR_T<Database> importDB(new SqliteDatabase(file.c_str()));
	importDB->connect();

	leveldb::DB* db = theApp->getHashNodeLDB();
	leveldb::WriteOptions wo;

	int count = 0;

	SQL_FOREACH(importDB, "SELECT * FROM CommittedObjects;")
	{
		uint256 hash;
		std::string hashStr;
		importDB->getStr("Hash", hashStr);
		hash.SetHexExact(hashStr);
		if (hash.isZero())
		{
			WriteLog (lsWARNING, HashedObject) << "zero hash found in import table";
		}
		else
		{
			Blob rawData;
			int size = importDB->getBinary("Object", NULL, 0);
			rawData.resize(9 + size);
			unsigned char* bufPtr = &rawData.front();

			importDB->getBinary("Object", bufPtr + 9, size);

			uint32 index = importDB->getBigInt("LedgerIndex");
			*reinterpret_cast<uint32*>(bufPtr + 0) = ntohl(index);
			*reinterpret_cast<uint32*>(bufPtr + 4) = ntohl(index);

			std::string type;
			importDB->getStr("ObjType", type);
			HashedObjectType htype = hotUNKNOWN;
			switch (type[0])
			{
				case 'L': htype = hotLEDGER; break;
				case 'T': htype = hotTRANSACTION; break;
				case 'A': htype = hotACCOUNT_NODE; break;
				case 'N': htype = hotTRANSACTION_NODE; break;
				default:
					assert(false);
					WriteLog (lsERROR, HashedObject) << "Invalid hashed object";
			}
			*(bufPtr + 8) = static_cast<unsigned char>(htype);

			leveldb::Status st = db->Put(wo,
				leveldb::Slice(reinterpret_cast<const char *>(hash.begin()), hash.size()),
				leveldb::Slice(reinterpret_cast<const char *>(bufPtr), rawData.size()));
			if (!st.ok())
			{
				WriteLog (lsFATAL, HashedObject) << "Failed to store hash node";
				assert(false);
			}
			++count;
		}
		if ((count % 10000) == 0)
		{
			WriteLog (lsINFO, HashedObject) << "Import in progress: " << count;
		}
	}

	WriteLog (lsWARNING, HashedObject) << "Imported " << count << " nodes";
	return count;
}

// vim:ts=4
