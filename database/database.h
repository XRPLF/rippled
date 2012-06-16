#ifndef __DATABASE__
#define __DATABASE__

#include <string>
#include <vector>
#include "../src/types.h"
#include "../src/utils.h"

#define SQL_FOREACH(_db, _strQuery)		\
	if ((_db)->executeSQL(_strQuery))	\
	for (bool _bMore = (_db)->startIterRows(); _bMore; _bMore = (_db)->getNextRow())

/*
	this maintains the connection to the database
*/
class Database
{
protected:
	int mNumCol;
	std::string mUser;
	std::string mHost;
	std::string mDBPass;
	std::vector<std::string> mColNameTable;

	bool getColNumber(const char* colName, int* retIndex);

public:
	Database(const char* host,const char* user,const char* pass);
	static Database* newMysqlDatabase(const char* host,const char* user,const char* pass);
	virtual ~Database();

	virtual void connect()=0;
	virtual void disconnect()=0;

	std::string& getPass(){ return(mDBPass); }

	virtual void escape(const unsigned char* start,int size,std::string& retStr)=0;
	std::string escape(const std::string strValue);

	// returns true if the query went ok
	virtual bool executeSQL(const char* sql, bool fail_okay=false)=0;

	bool executeSQL(std::string strSql, bool fail_okay=false) {
	    return executeSQL(strSql.c_str(), fail_okay);
	}

	// tells you how many rows were changed by an update or insert
	virtual int getNumRowsAffected()=0;
	virtual int getLastInsertID()=0;

	// returns false if there are no results
	virtual bool startIterRows()=0;
	virtual void endIterRows()=0;

	// call this after you executeSQL
	// will return false if there are no more rows
	virtual bool getNextRow()=0;

	// get Data from the current row
	bool getNull(const char* colName);
	char* getStr(const char* colName,std::string& retStr);
	std::string getStrBinary(const std::string& strColName);
	int32 getInt(const char* colName);
	float getFloat(const char* colName);
	bool getBool(const char* colName);

	// returns amount stored in buf
	int getBinary(const char* colName, unsigned char* buf, int maxSize);
	std::vector<unsigned char> getBinary(const std::string& strColName);

	uint64 getBigInt(const char* colName);

	virtual bool getNull(int colIndex)=0;
	virtual char* getStr(int colIndex,std::string& retStr)=0;
	virtual int32 getInt(int colIndex)=0;
	virtual float getFloat(int colIndex)=0;
	virtual bool getBool(int colIndex)=0;
	virtual int getBinary(int colIndex,unsigned char* buf,int maxSize)=0;
	virtual uint64 getBigInt(int colIndex)=0;
	virtual std::vector<unsigned char> getBinary(int colIndex)=0;

	// int getSingleDBValueInt(const char* sql);
	// float getSingleDBValueFloat(const char* sql);
	// char* getSingleDBValueStr(const char* sql, std::string& retStr);
};

#endif

// vim:ts=4
