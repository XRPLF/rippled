//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_POSTGRESQL_SOURCE
#include "soci-postgresql.h"
#include <libpq/libpq-fs.h> // libpq
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

#ifdef SOCI_POSTGRESQL_NOPARAMS
#ifndef SOCI_POSTGRESQL_NOBINDBYNAME
#define SOCI_POSTGRESQL_NOBINDBYNAME
#endif // SOCI_POSTGRESQL_NOBINDBYNAME
#endif // SOCI_POSTGRESQL_NOPARAMS

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;


postgresql_blob_backend::postgresql_blob_backend(
    postgresql_session_backend & session)
    : session_(session), fd_(-1)
{
    // nothing to do here, the descriptor is open in the postFetch
    // method of the Into element
}

postgresql_blob_backend::~postgresql_blob_backend()
{
    lo_close(session_.conn_, fd_);
}

std::size_t postgresql_blob_backend::get_len()
{
    int const pos = lo_lseek(session_.conn_, fd_, 0, SEEK_END);
    if (pos == -1)
    {
        throw soci_error("Cannot retrieve the size of BLOB.");
    }

    return static_cast<std::size_t>(pos);
}

std::size_t postgresql_blob_backend::read(
    std::size_t offset, char * buf, std::size_t toRead)
{
    int const pos = lo_lseek(session_.conn_, fd_,
        static_cast<int>(offset), SEEK_SET);
    if (pos == -1)
    {
        throw soci_error("Cannot seek in BLOB.");
    }

    int const readn = lo_read(session_.conn_, fd_, buf, toRead);
    if (readn < 0)
    {
        throw soci_error("Cannot read from BLOB.");
    }

    return static_cast<std::size_t>(readn);
}

std::size_t postgresql_blob_backend::write(
    std::size_t offset, char const * buf, std::size_t toWrite)
{
    int const pos = lo_lseek(session_.conn_, fd_,
        static_cast<int>(offset), SEEK_SET);
    if (pos == -1)
    {
        throw soci_error("Cannot seek in BLOB.");
    }

    int const writen = lo_write(session_.conn_, fd_,
        const_cast<char *>(buf), toWrite);
    if (writen < 0)
    {
        throw soci_error("Cannot write to BLOB.");
    }

    return static_cast<std::size_t>(writen);
}

std::size_t postgresql_blob_backend::append(
    char const * buf, std::size_t toWrite)
{
    int const pos = lo_lseek(session_.conn_, fd_, 0, SEEK_END);
    if (pos == -1)
    {
        throw soci_error("Cannot seek in BLOB.");
    }

    int const writen = lo_write(session_.conn_, fd_,
        const_cast<char *>(buf), toWrite);
    if (writen < 0)
    {
        throw soci_error("Cannot append to BLOB.");
    }

    return static_cast<std::size_t>(writen);
}

void postgresql_blob_backend::trim(std::size_t /* newLen */)
{
    throw soci_error("Trimming BLOBs is not supported.");
}
