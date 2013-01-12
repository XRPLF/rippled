#include "SqliteDatabase.h"
#include "sqlite3.h"

#include <string.h>
#include <stdio.h>
#include <iostream>

#include <boost/thread.hpp>
#include <boost/foreach.hpp>
#include <boost/bind.hpp>

using namespace std;

SqliteDatabase::SqliteDatabase(const char* host) : Database(host,"",""), walRunning(false)
{
	mConnection		= NULL;
	mCurrentStmt	= NULL;
}

void SqliteDatabase::connect()
{
	int rc = sqlite3_open(mHost.c_str(), &mConnection);
	if (rc)
	{
		cout << "Can't open database: " << mHost << " " << rc << endl;
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
			cout << "SQL Perror:" << rc << endl;
			cout << "Statement: " << sql << endl;
			cout << "Error: " << sqlite3_errmsg(mConnection) << endl;
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
		assert((rc != SQLITE_BUSY) && (rc != SQLITE_LOCKED));
		mMoreRows = false;

		if (!fail_ok)
		{
#ifdef DEBUG
			cout << "SQL Serror:" << rc << endl;
			cout << "Statement: " << sql << endl;
			cout << "Error: " << sqlite3_errmsg(mConnection) << endl;
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
		cout << "SQL Rerror:" << rc << endl;
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

bool SqliteDatabase::setupCheckpointing()
{
	sqlite3_wal_hook(mConnection, SqliteWALHook, this);
	return true;
}

void SqliteDatabase::doHook(const char *db, int pages)
{
	if (pages < 256)
		return;
	boost::mutex::scoped_lock sl(walMutex);
	if (walDBs.insert(db).second && !walRunning)
	{
		walRunning = true;
		boost::thread(boost::bind(&SqliteDatabase::runWal, this)).detach();
	}
}

void SqliteDatabase::runWal()
{
	std::set<std::string> walSet;

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
			sqlite3_wal_checkpoint_v2(mConnection, db.c_str(), SQLITE_CHECKPOINT_PASSIVE, &log, &ckpt);
			std::cerr << "Checkpoint " << db << ": " << log << " of " << ckpt << std::endl;
		}
		walSet.clear();

	}
}

// vim:ts=4
