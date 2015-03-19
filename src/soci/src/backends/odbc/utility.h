//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_UTILITY_H_INCLUDED
#define SOCI_UTILITY_H_INCLUDED

#include "soci-backend.h"
#include <sstream>

namespace soci
{

inline void throw_odbc_error(SQLSMALLINT htype, SQLHANDLE hndl, char const * msg)
{
    SQLCHAR message[SQL_MAX_MESSAGE_LENGTH + 1];
    SQLCHAR sqlstate[SQL_SQLSTATE_SIZE + 1];
    SQLINTEGER sqlcode;
    SQLSMALLINT length, i;

    std::stringstream ss;

    i = 1;

    /* get multiple field settings of diagnostic record */
    while (SQLGetDiagRecA(htype,
                         hndl,
                         i,
                         sqlstate,
                         &sqlcode,
                         message,
                         SQL_MAX_MESSAGE_LENGTH + 1,
                         &length) == SQL_SUCCESS)
    {
        ss << std::endl << "SOCI ODBC Error: " << msg << std::endl
           << "SQLSTATE = " << sqlstate << std::endl
           << "Native Error Code = " << sqlcode << std::endl
           << message << std::endl;
        ++i;
    }

    throw soci_error(ss.str());
}

inline bool is_odbc_error(SQLRETURN rc)
{
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
    {
        return true;
    }
    else
    {
        return false;
    }
}

}

#endif // SOCI_UTILITY_H_INCLUDED
