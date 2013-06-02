
#ifndef RIPPLE_SQLITEDATABASE_H
#define RIPPLE_SQLITEDATABASE_H

#include "database.h"

#include <string>
#include <set>

#include <boost/thread/mutex.hpp>

struct sqlite3;
struct sqlite3_stmt;


class SqliteDatabase : public Database
{
	sqlite3* mConnection;
	sqlite3* mAuxConnection;
	sqlite3_stmt* mCurrentStmt;
	bool mMoreRows;

	boost::mutex			walMutex;
	JobQueue*				mWalQ;
	bool					walRunning;

public:
	SqliteDatabase(const char* host);

	void connect();
	void disconnect();

	// returns true if the query went ok
	bool executeSQL(const char* sql, bool fail_okay);

	// tells you how many rows were changed by an update or insert
	int getNumRowsAffected();
	int getLastInsertID();

	// returns false if there are no results
	bool startIterRows(bool finalize);
	void endIterRows();

	// call this after you executeSQL
	// will return false if there are no more rows
	bool getNextRow(bool finalize);

	bool getNull(int colIndex);
	char* getStr(int colIndex,std::string& retStr);
	int32 getInt(int colIndex);
	float getFloat(int colIndex);
	bool getBool(int colIndex);
	// returns amount stored in buf
	int getBinary(int colIndex,unsigned char* buf,int maxSize);
	std::vector<unsigned char> getBinary(int colIndex);
	uint64 getBigInt(int colIndex);

	sqlite3* peekConnection() { return mConnection; }
	sqlite3*	getAuxConnection();
	virtual bool setupCheckpointing(JobQueue*);
	virtual SqliteDatabase* getSqliteDB() { return this; }

	void runWal();
	void doHook(const char *db, int walSize);

	int getKBUsedDB();
	int getKBUsedAll();
};

class SqliteStatement
{
private:
	SqliteStatement(const SqliteStatement&);				// no implementation
	SqliteStatement& operator=(const SqliteStatement&);		// no implementation

protected:
	sqlite3_stmt* statement;

public:
	SqliteStatement(SqliteDatabase* db, const char *statement, bool aux = false);
	SqliteStatement(SqliteDatabase* db, const std::string& statement, bool aux = false);
	~SqliteStatement();

	sqlite3_stmt* peekStatement();

	// positions start at 1
	int bind(int position, const void *data, int length);
	int bindStatic(int position, const void *data, int length);
	int bindStatic(int position, const std::vector<unsigned char>& value);

	int bind(int position, const std::string& value);
	int bindStatic(int position, const std::string& value);

	int bind(int position, uint32 value);
	int bind(int position);

	// columns start at 0
	int size(int column);

	const void* peekBlob(int column);
	std::vector<unsigned char> getBlob(int column);

	std::string getString(int column);
	const char* peekString(int column);
	uint32 getUInt32(int column);
	int64 getInt64(int column);

	int step();
	int reset();

	// translate return values of step and reset
	bool isOk(int);
	bool isDone(int);
	bool isRow(int);
	bool isError(int);
	std::string getError(int);
};

#endif

// vim:ts=4
