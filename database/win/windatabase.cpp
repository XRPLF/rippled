#include "windatabase.h"
#include "dbutility.h"

using namespace std;

Database* Database::newMysqlDatabase(const char* host,const char* user,const char* pass)
{
	return(new WinDatabase(host,user,pass));
}

WinDatabase::WinDatabase(const char* host,const char* user,const char* pass) : Database(host,user,pass)
{
	
}

WinDatabase::~WinDatabase()
{
	disconnect();
}


void WinDatabase::connect()
{
	SQLRETURN  rc;

	rc = SQLAllocHandle(SQL_HANDLE_ENV,SQL_NULL_HANDLE,&henv);
	myenv(henv, rc);

	rc = SQLSetEnvAttr(henv,SQL_ATTR_ODBC_VERSION,(SQLPOINTER)SQL_OV_ODBC3,0);
	myenv(henv, rc);

	rc = SQLAllocHandle(SQL_HANDLE_DBC,henv, &hdbc);   
	myenv(henv, rc);

	rc = SQLConnect(hdbc, (unsigned char*)(char*) mHost.c_str(), SQL_NTS, (unsigned char*)(char*) mUser.c_str(), SQL_NTS, (unsigned char*)(char*) mDBPass.c_str(), SQL_NTS);
	mycon(hdbc, rc);

	rc = SQLSetConnectAttr(hdbc,SQL_ATTR_AUTOCOMMIT,(SQLPOINTER)SQL_AUTOCOMMIT_ON,0);
	mycon(hdbc,rc);

	rc = SQLAllocHandle(SQL_HANDLE_STMT,hdbc,&hstmt);
	mycon(hdbc,rc);

	//rc = SQLGetInfo(hdbc,SQL_DBMS_NAME,&server_name,40,NULL);
	//mycon(hdbc, rc);

	//theUI->statusMsg("Connection Established to DB");
}

void WinDatabase::disconnect()
{
	SQLRETURN  rc;

	rc = SQLFreeStmt(hstmt, SQL_DROP);
	mystmt(hstmt,rc);

	rc = SQLDisconnect(hdbc); 
	mycon(hdbc, rc);

	rc = SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
	mycon(hdbc, rc);

	rc = SQLFreeHandle(SQL_HANDLE_ENV, henv);
	myenv(henv, rc);
}

int WinDatabase::getNumRowsAffected()
{
//	theUI->statusMsg("getNumRowsAffected()");
	SQLINTEGER ret;
	SQLRowCount(hstmt,&ret);
	return(ret);
}

// returns true if the query went ok
bool WinDatabase::executeSQL(const char* sql, bool fail_okay)
{
	SQLRETURN rc = SQLExecDirect(hstmt,(unsigned char*) sql,SQL_NTS);
	if(rc==SQL_ERROR)
	{
		//theUI->errorMsg("Trying to recover from DB error");
		rc = SQLExecDirect(hstmt,(unsigned char*) sql,SQL_NTS);
		if(rc==SQL_ERROR)
		{
			SQLCHAR       SqlState[6], /*SQLStmt[100],*/ Msg[SQL_MAX_MESSAGE_LENGTH];
			SQLINTEGER    NativeError;
			SQLSMALLINT   i, MsgLen;
			SQLRETURN     /*rc1,*/ rc2;

			i = 1;
			while ((rc2 = SQLGetDiagRec(SQL_HANDLE_STMT, hstmt, i, SqlState, &NativeError, Msg, sizeof(Msg), &MsgLen)) != SQL_NO_DATA) 
			{
				//theUI->errorMsg("DB ERROR: %s,%d,%s",SqlState,NativeError,Msg);
				i++;
			}
				
			return(false);
		}
	}

	mystmt(hstmt,rc);

	// commit the transaction 
	rc = SQLEndTran(SQL_HANDLE_DBC, hdbc, SQL_COMMIT); 
	mycon(hdbc,rc);
	
	return(true);
	
}

bool WinDatabase::startIterRows()
{
	SQLUINTEGER pcColDef;
	SQLCHAR     szColName[MAX_NAME_LEN];
	SQLSMALLINT pfSqlType, pcbScale, pfNullable;
	SQLSMALLINT numCol;

	/* get total number of columns from the resultset */
	SQLRETURN rc = SQLNumResultCols(hstmt,&numCol);
	mystmt(hstmt,rc);
	mNumCol=(int)numCol;

	if(mNumCol)
	{
		mColNameTable.resize(mNumCol);

		// fill out the column name table
		for(int n = 1; n <= mNumCol; n++)
		{
			rc = SQLDescribeCol(hstmt,n,szColName, MAX_NAME_LEN, NULL, &pfSqlType,&pcColDef,&pcbScale,&pfNullable);
			mystmt(hstmt,rc);

			mColNameTable[n-1]= (char*)szColName;
		}
		return(true);
	}
	return(false);
}

// call this after you executeSQL
// will return false if there are no more rows
bool WinDatabase::getNextRow()
{
	SQLRETURN rc = SQLFetch(hstmt);
	return((rc==SQL_SUCCESS) || (rc==SQL_SUCCESS_WITH_INFO));
}



char* WinDatabase::getStr(int colIndex,string& retStr)
{
	colIndex++;
	retStr="";
	char buf[1000];
//	SQLINTEGER len;
	buf[0]=0;

	while(SQLGetData(hstmt, colIndex, SQL_C_CHAR, &buf, 1000,NULL)!= SQL_NO_DATA) 
	{
		retStr += buf;
//		theUI->statusMsg("Win: %s",buf);
	}

	//SQLRETURN rc = SQLGetData(hstmt,colIndex,SQL_C_CHAR,&buf,30000,&len);
	//mystmt(hstmt,rc);
	//*retStr=buf;

	//theUI->statusMsg("Win: %s",buf);
	
	return((char*)retStr.c_str());
}

int32 WinDatabase::getInt(int colIndex)
{
	colIndex++;
	int ret=0;
	SQLRETURN rc = SQLGetData(hstmt,colIndex,SQL_INTEGER,&ret,sizeof(int),NULL);
	mystmt(hstmt,rc);
	return(ret);
}

float WinDatabase::getFloat(int colIndex)
{
	colIndex++;
	float ret=0;
	SQLRETURN rc = SQLGetData(hstmt,colIndex,SQL_C_FLOAT,&ret,sizeof(float),NULL);
	mystmt(hstmt,rc);

	return(ret);
}

bool WinDatabase::getBool(int colIndex)
{
	colIndex++;
	char buf[1];
	buf[0]=0;
	SQLRETURN rc = SQLGetData(hstmt,colIndex,SQL_C_CHAR,&buf,1,NULL);
	mystmt(hstmt,rc);

	return(buf[0] != 0);
}


void WinDatabase::endIterRows()
{
	// free the statement row bind resources 
	SQLRETURN rc = SQLFreeStmt(hstmt, SQL_UNBIND);
	mystmt(hstmt,rc);

	// free the statement cursor 
	rc = SQLFreeStmt(hstmt, SQL_CLOSE);
	mystmt(hstmt,rc);
}

// TODO
void WinDatabase::escape(const unsigned char* start,int size,std::string& retStr)
{
	retStr=(char*)start;
}

// TODO
int WinDatabase::getLastInsertID()
{
	return(0);
}

uint64 WinDatabase::getBigInt(int colIndex)
{
	colIndex++;
	uint64 ret=0;
	SQLRETURN rc = SQLGetData(hstmt,colIndex,SQL_INTEGER,&ret,sizeof(uint64),NULL);
	mystmt(hstmt,rc);
	return(ret);
}
// TODO:
int WinDatabase::getBinary(int colIndex,unsigned char* buf,int maxSize)
{
	return(0);
}

std::vector<unsigned char> WinDatabase::getBinary(int colIndex)
{
	// TODO:
	std::vector<unsigned char>	vucResult;
	return vucResult;
}