#include "mysqldatabase.h"
#include "reportingmechanism.h"
#include "string/i4string.h"
#include <stdlib.h>


Database* Database::newDatabase(const char* host,const char* user,const char* pass)
{
	return(new MySqlDatabase(host,user,pass));
}

MySqlDatabase::MySqlDatabase(const char* host,const char* user,const char* pass) : Database(host,user,pass)
{
	
}

MySqlDatabase::~MySqlDatabase()
{
}

void MySqlDatabase::connect()
{
	mysql_init(&mMysql);
	mysql_options(&mMysql,MYSQL_READ_DEFAULT_GROUP,"i4min");
	if(!mysql_real_connect(&mMysql,mHost,mUser,mDBPass,NULL,0,NULL,0))
	{
		theUI->statusMsg("Failed to connect to database: Error: %s\n", mysql_error(&mMysql));
	}else theUI->statusMsg("Connection Established to DB");
}

void MySqlDatabase::disconnect()
{
	mysql_close(&mMysql);
}

int MySqlDatabase::getNumRowsAffected()
{
	return( mysql_affected_rows(&mMysql));
}

// returns true if the query went ok
bool MySqlDatabase::executeSQL(const char* sql)
{
	int ret=mysql_query(&mMysql, sql);
	if(ret)
	{
		connect();
		int ret=mysql_query(&mMysql, sql);
		if(ret)
		{
			theUI->statusMsg("ERROR with executeSQL: %d %s",ret,sql);
			return(false);
		}
	}
	return(true);
}

bool MySqlDatabase::startIterRows()
{
	mResult=mysql_store_result(&mMysql);
	// get total number of columns from the resultset 
	mNumCol = mysql_num_fields(mResult);
	if(mNumCol)
	{
		delete[](mColNameTable);
		mColNameTable=new i4_str[mNumCol];

		// fill out the column name table
		for(int n = 0; n < mNumCol; n++)
		{
			MYSQL_FIELD* field=mysql_fetch_field(mResult);
			mColNameTable[n]= field->name;
		}
		return(true);
	}
	return(false);
}

// call this after you executeSQL
// will return false if there are no more rows
bool MySqlDatabase::getNextRow()
{
	mCurRow=mysql_fetch_row(mResult);

	return(mCurRow!=NULL);
}

char* MySqlDatabase::getStr(int colIndex,i4_str* retStr)
{
	if(mCurRow[colIndex])
	{
		(*retStr)=mCurRow[colIndex];
	}else (*retStr)="";

	return(*retStr);
}

w32 MySqlDatabase::getInt(int colIndex)
{
	
	if(mCurRow[colIndex])
	{
		w32 ret=atoll(mCurRow[colIndex]);
		//theUI->statusMsg("getInt: %s,%c,%u",mCurRow[colIndex],mCurRow[colIndex][0],ret);
		return(ret);
	}
	return(0);
}

float MySqlDatabase::getFloat(int colIndex)
{
	if(mCurRow[colIndex])
	{
		float ret=atof(mCurRow[colIndex]);
		return(ret);
	}
	return(0.0);
}

bool MySqlDatabase::getBool(int colIndex)
{
	if(mCurRow[colIndex])
	{
		int ret=atoi(mCurRow[colIndex]);
		return(ret);
	}
	return(false);
}


void MySqlDatabase::endIterRows()
{
	mysql_free_result(mResult);
}


