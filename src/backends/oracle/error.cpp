//
// Copyright (C) 2004-2007 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_ORACLE_SOURCE
#include "soci/oracle/soci-oracle.h"
#include "error.h"
#include <limits>
#include <sstream>

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;
using namespace soci::details::oracle;

oracle_soci_error::oracle_soci_error(std::string const & msg, int errNum)
    : soci_error(msg), err_num_(errNum), cat_(unknown)
{
    if (errNum == 12162 || errNum == 25403)
    {
        cat_ = connection_error;
    }
    else if (errNum == 1400)
    {
        cat_ = constraint_violation;
    }
    else if (errNum == 1466 ||
        errNum == 2055 ||
        errNum == 2067 ||
        errNum == 2091 ||
        errNum == 2092 ||
        errNum == 25401 ||
        errNum == 25402 ||
        errNum == 25405 ||
        errNum == 25408 ||
        errNum == 25409)
    {
        cat_ = unknown_transaction_state;
    }
}

void soci::details::oracle::get_error_details(sword res, OCIError *errhp,
    std::string &msg, int &errNum)
{
    text errbuf[512];
    sb4 errcode;
    errNum = 0;

    switch (res)
    {
    case OCI_NO_DATA:
        msg = "soci error: No data";
        break;
    case OCI_ERROR:
    case OCI_SUCCESS_WITH_INFO:
        OCIErrorGet(errhp, 1, 0, &errcode,
             errbuf, sizeof(errbuf), OCI_HTYPE_ERROR);
        msg = reinterpret_cast<char*>(errbuf);
        errNum = static_cast<int>(errcode);
        break;
    case OCI_INVALID_HANDLE:
        msg = "soci error: Invalid handle";
        break;
    default:
        msg = "soci error: Unknown error code";
    }
}

void soci::details::oracle::throw_oracle_soci_error(sword res, OCIError *errhp)
{
    std::string msg;
    int errNum;

    get_error_details(res, errhp, msg, errNum);
    throw oracle_soci_error(msg, errNum);
}
