#include "database.h"

class SqliteDatabase : public Database
{
public:
	SqliteDatabase();


	void escape(unsigned char* start,int size,std::string& retStr);

};