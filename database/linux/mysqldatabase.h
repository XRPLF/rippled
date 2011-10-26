#ifndef __MYSQLDATABASE__
#define __MYSQLDATABASE__

#include "../database.h"
#include "string/i4string.h"
#include "mysql.h"

/*
this maintains the connection to the database
*/
class MySqlDatabase : public Database
{
	MYSQL mMysql;
	MYSQL_RES* mResult;
	MYSQL_ROW mCurRow;

public:
	MySqlDatabase(const char* host,const char* user,const char* pass);
	~MySqlDatabase();

	void connect();
	void disconnect();

	// returns true if the query went ok
	bool executeSQL(const char* sql);

	int getNumRowsAffected();

	// returns false if there are no results
	bool startIterRows();
	void endIterRows();

	// call this after you executeSQL
	// will return false if there are no more rows
	bool getNextRow();

	// get Data from the current row

	char* getStr(int colIndex,i4_str* retStr);
	w32 getInt(int colIndex);
	float getFloat(int colIndex);
	bool getBool(int colIndex);
};


#endif

