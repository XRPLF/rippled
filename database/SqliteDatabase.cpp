#include "SqliteDatabase.h"

SqliteDatabase::SqliteDatabase()
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