#ifndef __WINDATABASE__
#define __WINDATABASE__

#include "../database.h"


#ifdef WIN32
#define _WINSOCKAPI_   // prevent inclusion of winsock.h in windows.h
#include <windows.h>
#include "sql.h"
#endif 


/*
	this maintains the connection to the database
*/
class WinDatabase : public Database
{
	SQLHENV    henv;
	SQLHDBC    hdbc;
	SQLHSTMT   hstmt;

public:
	WinDatabase(const char* host,const char* user,const char* pass);
	virtual ~WinDatabase();

	void connect();
	void disconnect();

	//char* getPass(){ return((char*)mDBPass.c_str()); }
	
	// returns true if the query went ok
	bool executeSQL(const char* sql);

	int getNumRowsAffected();
	int getLastInsertID();

	// returns false if there are no results
	bool startIterRows();
	void endIterRows();

	// call this after you executeSQL
	// will return false if there are no more rows
	bool getNextRow();

	// get Data from the current row
	char* getStr(int colIndex,std::string& retStr);
	int32 getInt(int colIndex);
	float getFloat(int colIndex);
	bool getBool(int colIndex);
	uint64 getBigInt(int colIndex);
	bool getBinary(int colIndex,unsigned char* buf,int maxSize);

	void escape(unsigned char* start,int size,std::string& retStr);
};


#endif

