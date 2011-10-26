#ifndef __TMYODBC_UTILITY_H__
#define __TMYODBC_UTILITY_H__

#ifdef HAVE_CONFIG_H
#include <myconf.h>
#endif


/* STANDARD C HEADERS */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/* ODBC HEADERS */
#include <sqlext.h>

#define MAX_NAME_LEN 95
#define MAX_COLUMNS 255
#define MAX_ROW_DATA_LEN 255


/* PROTOTYPE */
void myerror(SQLRETURN rc,SQLSMALLINT htype, SQLHANDLE handle);

/* UTILITY MACROS */
#define myenv(henv,r)  \
        if ( ((r) != SQL_SUCCESS) ) \
            myerror(r, 1,henv); \
        assert( ((r) == SQL_SUCCESS) || ((r) == SQL_SUCCESS_WITH_INFO) )

#define myenv_err(henv,r,rc)  \
        if ( rc == SQL_ERROR || rc == SQL_SUCCESS_WITH_INFO ) \
            myerror(rc, 1, henv); \
        assert( r )

#define mycon(hdbc,r)  \
        if ( ((r) != SQL_SUCCESS) ) \
            myerror(r, 2, hdbc); \
        assert( ((r) == SQL_SUCCESS) || ((r) == SQL_SUCCESS_WITH_INFO) )

#define mycon_err(hdbc,r,rc)  \
        if ( rc == SQL_ERROR || rc == SQL_SUCCESS_WITH_INFO ) \
            myerror(rc, 2, hdbc); \
        assert( r )

#define mystmt(hstmt,r)  \
        if ( ((r) != SQL_SUCCESS) ) \
            myerror(r, 3, hstmt); \
        assert( ((r) == SQL_SUCCESS) || ((r) == SQL_SUCCESS_WITH_INFO) )

#define mystmt_err(hstmt,r,rc)  \
        if ( rc == SQL_ERROR || rc == SQL_SUCCESS_WITH_INFO ) \
            myerror(rc, 3, hstmt); \
        assert( r )

/********************************************************
* MyODBC 3.51 error handler                             *
*********************************************************/
void myerror(SQLRETURN rc, SQLSMALLINT htype, SQLHANDLE handle)
{
  SQLRETURN lrc;

  if( rc == SQL_ERROR || rc == SQL_SUCCESS_WITH_INFO ) 
  {
    SQLCHAR     szSqlState[6],szErrorMsg[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER  pfNativeError;
    SQLSMALLINT pcbErrorMsg;
    
    lrc = SQLGetDiagRec(htype, handle,1,    
                        (SQLCHAR *)&szSqlState,
                        (SQLINTEGER *)&pfNativeError,
                        (SQLCHAR *)&szErrorMsg,
                         SQL_MAX_MESSAGE_LENGTH-1,
                        (SQLSMALLINT *)&pcbErrorMsg);
    if(lrc == SQL_SUCCESS || lrc == SQL_SUCCESS_WITH_INFO)
      printf("\n [%s][%d:%s]\n",szSqlState,pfNativeError,szErrorMsg);
  }
}


#endif /* __TMYODBC_UTILITY_H__ */

