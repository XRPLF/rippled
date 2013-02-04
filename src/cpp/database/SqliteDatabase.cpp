#include "SqliteDatabase.h"
#include "sqlite3.h"

#include <string.h>
#include <stdio.h>
#include <iostream>

#include <boost/thread.hpp>
#include <boost/foreach.hpp>
#include <boost/bind.hpp>

#include "../ripple/JobQueue.h"
#include "../ripple/Log.h"

SETUP_NLOG("DataBase");

using namespace std;

SqliteDatabase::SqliteDatabase(const char* host) : Database(host,"",""), mWalQ(NULL), walRunning(false)
{
	mConnection		= NULL;
	mCurrentStmt	= NULL;
}

void SqliteDatabase::connect()
{
	int rc = sqlite3_open(mHost.c_str(), &mConnection);
	if (rc)
	{
		cLog(lsFATAL) << "Can't open " << mHost << " " << rc;
		sqlite3_close(mConnection);
		assert((rc != SQLITE_BUSY) && (rc != SQLITE_LOCKED));
	}
}

void SqliteDatabase::disconnect()
{
	sqlite3_finalize(mCurrentStmt);
	sqlite3_close(mConnection);
}

// returns true if the query went ok
bool SqliteDatabase::executeSQL(const char* sql, bool fail_ok)
{
	sqlite3_finalize(mCurrentStmt);

	int rc = sqlite3_prepare_v2(mConnection, sql, -1, &mCurrentStmt, NULL);

	if (SQLITE_OK != rc)
	{
		if (!fail_ok)
		{
#ifdef DEBUG
			cLog(lsWARNING) << "Perror:" << mHost << ": " << rc;
			cLog(lsWARNING) << "Statement: " << sql;
			cLog(lsWARNING) << "Error: " << sqlite3_errmsg(mConnection);
#endif
		}
		return false;
	}
	rc = sqlite3_step(mCurrentStmt);
	if (rc == SQLITE_ROW)
	{
		mMoreRows = true;
	}
	else if (rc == SQLITE_DONE)
	{
		mMoreRows = false;
	}
	else
	{
		if ((rc != SQLITE_BUSY) && (rc != SQLITE_LOCKED))
		{
			cLog(lsFATAL) << mHost  << " returns error " << rc << ": " << sqlite3_errmsg(mConnection);
			assert(false);
		}
		mMoreRows = false;

		if (!fail_ok)
		{
#ifdef DEBUG
			cLog(lsWARNING) << "SQL Serror:" << mHost << ": " << rc;
			cLog(lsWARNING) << "Statement: " << sql;
			cLog(lsWARNING) << "Error: " << sqlite3_errmsg(mConnection);
#endif
		}
		return false;
	}

	return true;
}

// tells you how many rows were changed by an update or insert
int SqliteDatabase::getNumRowsAffected()
{
	// TODO: SqliteDatabase::getNumRowsAffected()
	return(0);
}

int SqliteDatabase::getLastInsertID()
{
	return(sqlite3_last_insert_rowid(mConnection));
}

// returns false if there are no results
bool SqliteDatabase::startIterRows()
{
	mColNameTable.clear();
	mColNameTable.resize(sqlite3_column_count(mCurrentStmt));
	for(unsigned n=0; n<mColNameTable.size(); n++)
	{
		mColNameTable[n]=sqlite3_column_name(mCurrentStmt,n);
	}

	return(mMoreRows);
}

void SqliteDatabase::endIterRows()
{
	sqlite3_finalize(mCurrentStmt);
	mCurrentStmt=NULL;
}

// call this after you executeSQL
// will return false if there are no more rows
bool SqliteDatabase::getNextRow()
{
	if (!mMoreRows) return(false);

	int rc=sqlite3_step(mCurrentStmt);
	if (rc==SQLITE_ROW)
	{
		return(true);
	}
	else if (rc==SQLITE_DONE)
	{
		return(false);
	}
	else
	{
		assert((rc != SQLITE_BUSY) && (rc != SQLITE_LOCKED));
		cLog(lsWARNING) << "Rerror: " << mHost << ": " << rc;
		return(false);
	}
}

bool SqliteDatabase::getNull(int colIndex)
{
    return(SQLITE_NULL == sqlite3_column_type(mCurrentStmt, colIndex));
}

char* SqliteDatabase::getStr(int colIndex,std::string& retStr)
{
	retStr=(char*)sqlite3_column_text(mCurrentStmt, colIndex);
	return((char*)retStr.c_str());
}

int32 SqliteDatabase::getInt(int colIndex)
{
	return(sqlite3_column_int(mCurrentStmt, colIndex));
}

float SqliteDatabase::getFloat(int colIndex)
{
	return(sqlite3_column_double(mCurrentStmt, colIndex));
}

bool SqliteDatabase::getBool(int colIndex)
{
	return(sqlite3_column_int(mCurrentStmt, colIndex));
}

int SqliteDatabase::getBinary(int colIndex,unsigned char* buf,int maxSize)
{
	const void* blob=sqlite3_column_blob(mCurrentStmt, colIndex);
	int size=sqlite3_column_bytes(mCurrentStmt, colIndex);
	if(size<maxSize) maxSize=size;
	memcpy(buf,blob,maxSize);
	return(size);
}

std::vector<unsigned char> SqliteDatabase::getBinary(int colIndex)
{
	const unsigned char*		blob	= reinterpret_cast<const unsigned char*>(sqlite3_column_blob(mCurrentStmt, colIndex));
	size_t						iSize	= sqlite3_column_bytes(mCurrentStmt, colIndex);
	std::vector<unsigned char>	vucResult;

	vucResult.resize(iSize);
	std::copy(blob, blob+iSize, vucResult.begin());

	return vucResult;
}

uint64 SqliteDatabase::getBigInt(int colIndex)
{
	return(sqlite3_column_int64(mCurrentStmt, colIndex));
}


static int SqliteWALHook(void *s, sqlite3* dbCon, const char *dbName, int walSize)
{
	(reinterpret_cast<SqliteDatabase*>(s))->doHook(dbName, walSize);
	return SQLITE_OK;
}

bool SqliteDatabase::setupCheckpointing(JobQueue *q)
{
	mWalQ = q;
	sqlite3_wal_hook(mConnection, SqliteWALHook, this);
	return true;
}

void SqliteDatabase::doHook(const char *db, int pages)
{
	if (pages < 512)
		return;
	boost::mutex::scoped_lock sl(walMutex);
	if (walDBs.insert(db).second && !walRunning)
	{
		walRunning = true;
		if (mWalQ)
			mWalQ->addJob(jtWAL, boost::bind(&SqliteDatabase::runWal, this));
		else
			boost::thread(boost::bind(&SqliteDatabase::runWal, this)).detach();
	}
}

void SqliteDatabase::runWal()
{
	std::set<std::string> walSet;
	std::string name = sqlite3_db_filename(mConnection, "main");

	while (1)
	{
		{
			boost::mutex::scoped_lock sl(walMutex);
			walDBs.swap(walSet);
			if (walSet.empty())
			{
				walRunning = false;
				return;
			}
		}

		BOOST_FOREACH(const std::string& db, walSet)
		{
			int log, ckpt;
			int ret = sqlite3_wal_checkpoint_v2(mConnection, db.c_str(), SQLITE_CHECKPOINT_PASSIVE, &log, &ckpt);
			if (ret != SQLITE_OK)
			{
				cLog((ret == SQLITE_LOCKED) ? lsDEBUG : lsWARNING) << "WAL " << mHost << ":"
					<< db << " errror " << ret;
			}
		}
		walSet.clear();
	}
}

SqliteStatement::SqliteStatement(SqliteDatabase* db, const char *sql)
{
	assert(db);
	int j = sqlite3_prepare_v2(db->peekConnection(), sql, strlen(sql) + 1, &statement, NULL);
	if (j != SQLITE_OK)
		throw j;
}

SqliteStatement::~SqliteStatement()
{
	sqlite3_finalize(statement);
}

sqlite3_stmt* SqliteStatement::peekStatement()
{
	return statement;
}

int SqliteStatement::bind(int position, const void *data, int length)
{
	return sqlite3_bind_blob(statement, position, data, length, SQLITE_TRANSIENT);
}

int SqliteStatement::bindStatic(int position, const void *data, int length)
{
	return sqlite3_bind_blob(statement, position, data, length, SQLITE_STATIC);
}

int SqliteStatement::bindStatic(int position, const std::vector<unsigned char>& value)
{
	return sqlite3_bind_blob(statement, position, &value.front(), value.size(), SQLITE_STATIC);
}

int SqliteStatement::bind(int position, uint32 value)
{
	return sqlite3_bind_int64(statement, position, static_cast<sqlite3_int64>(value));
}

int SqliteStatement::bind(int position, const std::string& value)
{
	return sqlite3_bind_text(statement, position, value.data(), value.size(), SQLITE_TRANSIENT);
}

int SqliteStatement::bindStatic(int position, const std::string& value)
{
	return sqlite3_bind_text(statement, position, value.data(), value.size(), SQLITE_STATIC);
}

int SqliteStatement::bind(int position)
{
	return sqlite3_bind_null(statement, position);
}

int SqliteStatement::size(int column)
{
	return sqlite3_column_bytes(statement, column);
}

const void* SqliteStatement::peekBlob(int column)
{
	return sqlite3_column_blob(statement, column);
}

std::vector<unsigned char> SqliteStatement::getBlob(int column)
{
	int size = sqlite3_column_bytes(statement, column);
	std::vector<unsigned char> ret(size);
	memcpy(&(ret.front()), sqlite3_column_blob(statement, column), size);
	return ret;
}

std::string SqliteStatement::getString(int column)
{
	return reinterpret_cast<const char *>(sqlite3_column_text(statement, column));
}

const char* SqliteStatement::peekString(int column)
{
	return reinterpret_cast<const char *>(sqlite3_column_text(statement, column));
}

uint32 SqliteStatement::getUInt32(int column)
{
	return static_cast<uint32>(sqlite3_column_int64(statement, column));
}

int64 SqliteStatement::getInt64(int column)
{
	return sqlite3_column_int64(statement, column);
}

int SqliteStatement::step()
{
	return sqlite3_step(statement);
}

int SqliteStatement::reset()
{
	return sqlite3_reset(statement);
}

bool SqliteStatement::isOk(int j)
{
	return j == SQLITE_OK;
}

bool SqliteStatement::isDone(int j)
{
	return j == SQLITE_DONE;
}

bool SqliteStatement::isRow(int j)
{
	return j == SQLITE_ROW;
}

// vim:ts=4
