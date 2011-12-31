#include "SqliteDatabase.h"
#include "sqlite3.h"
#include <string.h>
#include <stdio.h>
#include <iostream>
using namespace std;

SqliteDatabase::SqliteDatabase(const char* host) : Database(host,"","")
{
	mConnection=NULL;
	mCurrentStmt=NULL;
}

void SqliteDatabase::connect()
{
	
;	int rc = sqlite3_open(mHost.c_str(), &mConnection);
	if( rc )
	{
		cout << "Can't open database: " << mHost << " " << rc << endl;
		sqlite3_close(mConnection);
	}
}

void SqliteDatabase::disconnect()
{
	sqlite3_finalize(mCurrentStmt);
	sqlite3_close(mConnection);
}

// returns true if the query went ok
bool SqliteDatabase::executeSQL(const char* sql)
{
	sqlite3_finalize(mCurrentStmt);
	int rc=sqlite3_prepare_v2(mConnection,sql,-1,&mCurrentStmt,NULL);
	if( rc!=SQLITE_OK )
	{
		cout << "SQL Perror:" << rc << endl;
		return(false);
	}
	rc=sqlite3_step(mCurrentStmt);
	if(rc==SQLITE_ROW)
	{
		mMoreRows=true;
	}else if(rc==SQLITE_DONE)
	{
		mMoreRows=false;
	}else
	{
		mMoreRows=false;
		cout << "SQL Serror:" << rc << endl;
		return(false);
	}

	return(true);
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
	if(!mMoreRows) return(false);

	int rc=sqlite3_step(mCurrentStmt);
	if(rc==SQLITE_ROW)
	{
		return(true);
	}else if(rc==SQLITE_DONE)
	{
		return(false);
	}else
	{
		cout << "SQL Rerror:" << rc << endl;
		return(false);
	}
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

uint64 SqliteDatabase::getBigInt(int colIndex)
{
	return(sqlite3_column_int64(mCurrentStmt, colIndex));
}


/* http://www.sqlite.org/lang_expr.html
BLOB literals are string literals containing hexadecimal data and preceded by a single "x" or "X" character. For example:
X'53514C697465'
*/
void SqliteDatabase::escape(const unsigned char* start,int size,std::string& retStr)
{
	retStr.clear();

	char buf[3];
	retStr.append("X'");
	for(int n=0; n<size; n++)
	{
		sprintf(buf, "%x", start[n]);
		if(buf[1]==0)
		{
			retStr.append("0");
			retStr.append(buf);
		}else retStr.append(buf);

	}
	retStr.push_back('\'');
}