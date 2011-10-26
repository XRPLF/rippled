#include "database.h"
#include "../utillib/reportingmechanism.h"
#include "string/i4string.h"
#include <stdlib.h>


Database::Database(const char* host,const char* user,const char* pass) : mDBPass(pass), mHost(host) ,mUser(user), mNumCol(0)
{
	mColNameTable=NULL;
}

Database::~Database()
{
	delete[] mColNameTable;
	
}


char* Database::getStr(const char* colName,std::string& retStr)
{
	int index;
	if(getColNumber(colName,&index))
	{
		return(getStr(index,retStr));
	}
	return(NULL);
}

w32 Database::getInt(const char* colName)
{
	int index;
	if(getColNumber(colName,&index))
	{
		return(getInt(index));
	}
	return(0);
}

float Database::getFloat(const char* colName)
{
	int index;
	if(getColNumber(colName,&index))
	{
		return(getFloat(index));
	}
	return(0);
}

bool Database::getBool(const char* colName)
{
	int index;
	if(getColNumber(colName,&index))
	{
		return(getBool(index));
	}
	return(0);
}



// returns false if can't find col
bool Database::getColNumber(const char* colName,int* retIndex)
{
	for(int n=0; n<mNumCol; n++)
	{
		if(i4_stricmp(colName,mColNameTable[n])==0)
		{
			*retIndex=n;
			return(true);
		}
	}
	return(false);
}

int Database::getSingleDBValueInt(const char* sql)
{
	int ret;
	if( executeSQL(sql) && startIterRows() && getNextRow())
	{
		ret=getInt(0);
		endIterRows();
	}else 
	{
		//theUI->statusMsg("ERROR with database: %s",sql);
		ret=0;
	}
	return(ret);
}

float Database::getSingleDBValueFloat(const char* sql)
{
	float ret;
	if( executeSQL(sql) && startIterRows() && getNextRow())
	{
		ret=getFloat(0);
		endIterRows();
	}else 
	{
		//theUI->statusMsg("ERROR with database: %s",sql);
		ret=0;
	}
	return(ret);
}

char* Database::getSingleDBValueStr(const char* sql,std::string& retStr)
{
	char* ret;
	if( executeSQL(sql) && startIterRows() && getNextRow())
	{
		ret=getStr(0,retStr);
		endIterRows();
	}else 
	{
		//theUI->statusMsg("ERROR with database: %s",sql);
		ret=0;
	}
	return(ret);
}