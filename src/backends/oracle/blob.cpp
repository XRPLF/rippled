//
// Copyright (C) 2004-2007 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include "soci/oracle/soci-oracle.h"
#include "error.h"
#include "soci/statement.h"
#include <cstring>
#include <sstream>
#include <cstdio>
#include <ctime>
#include <cctype>

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;
using namespace soci::details::oracle;

oracle_blob_backend::oracle_blob_backend(oracle_session_backend &session)
    : session_(session)
{
    sword res = OCIDescriptorAlloc(session.envhp_,
        reinterpret_cast<dvoid**>(&lobp_), OCI_DTYPE_LOB, 0, 0);
    if (res != OCI_SUCCESS)
    {
        throw soci_error("Cannot allocate the LOB locator");
    }
}

oracle_blob_backend::~oracle_blob_backend()
{
    OCIDescriptorFree(lobp_, OCI_DTYPE_LOB);
}

std::size_t oracle_blob_backend::get_len()
{
    ub4 len;

    sword res = OCILobGetLength(session_.svchp_, session_.errhp_,
        lobp_, &len);

    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }

    return static_cast<std::size_t>(len);
}

std::size_t oracle_blob_backend::read(
    std::size_t offset, char *buf, std::size_t toRead)
{
    ub4 amt = static_cast<ub4>(toRead);

    sword res = OCILobRead(session_.svchp_, session_.errhp_, lobp_, &amt,
        static_cast<ub4>(offset), reinterpret_cast<dvoid*>(buf),
        amt, 0, 0, 0, 0);
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }

    return static_cast<std::size_t>(amt);
}

std::size_t oracle_blob_backend::write(
    std::size_t offset, char const *buf, std::size_t toWrite)
{
    ub4 amt = static_cast<ub4>(toWrite);

    sword res = OCILobWrite(session_.svchp_, session_.errhp_, lobp_, &amt,
        static_cast<ub4>(offset),
        reinterpret_cast<dvoid*>(const_cast<char*>(buf)),
        amt, OCI_ONE_PIECE, 0, 0, 0, 0);
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }

    return static_cast<std::size_t>(amt);
}

std::size_t oracle_blob_backend::append(char const *buf, std::size_t toWrite)
{
    ub4 amt = static_cast<ub4>(toWrite);

    sword res = OCILobWriteAppend(session_.svchp_, session_.errhp_, lobp_,
        &amt, reinterpret_cast<dvoid*>(const_cast<char*>(buf)),
        amt, OCI_ONE_PIECE, 0, 0, 0, 0);
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }

    return static_cast<std::size_t>(amt);
}

void oracle_blob_backend::trim(std::size_t newLen)
{
    sword res = OCILobTrim(session_.svchp_, session_.errhp_, lobp_,
        static_cast<ub4>(newLen));
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }
}
