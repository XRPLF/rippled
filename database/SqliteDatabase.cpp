#include "SqliteDatabase.h"

SqliteDatabase::SqliteDatabase() : Database
{

}

void SqliteDatabase::connect()
{

}
void SqliteDatabase::disconnect()
{

}

// returns true if the query went ok
bool SqliteDatabase::executeSQL(const char* sql)
{

}

// tells you how many rows were changed by an update or insert
int SqliteDatabase::getNumRowsAffected()
{

}

// returns false if there are no results
bool SqliteDatabase::startIterRows()
{

}
void SqliteDatabase::endIterRows()
{

}

// call this after you executeSQL
// will return false if there are no more rows
bool SqliteDatabase::getNextRow()
{

}

char* SqliteDatabase::getStr(int colIndex,std::string& retStr)
{

}
int32 SqliteDatabase::getInt(int colIndex)
{

}
float SqliteDatabase::getFloat(int colIndex)
{

}
bool SqliteDatabase::getBool(int colIndex)
{

}
bool SqliteDatabase::getBinary(int colIndex,unsigned char* buf,int maxSize)
{

}
uint64 SqliteDatabase::getBigInt(int colIndex)
{

}


/* http://www.sqlite.org/lang_expr.html
BLOB literals are string literals containing hexadecimal data and preceded by a single "x" or "X" character. For example:
X'53514C697465'
*/
void SqliteDatabase::escape(unsigned char* start,int size,std::string& retStr)
{
	retStr.clear();

	char buf[3];
	retStr.append("X'");
	for(int n=0; n<size; n++)
	{
		retStr.append( itoa(*start,buf,16) );
	}
	retStr.push_back('\'');
}