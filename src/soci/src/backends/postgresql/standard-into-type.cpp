//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_POSTGRESQL_SOURCE
#include "soci/soci-platform.h"
#include "soci/postgresql/soci-postgresql.h"
#include "soci-cstrtod.h"
#include "soci-mktime.h"
#include "common.h"
#include "soci/rowid.h"
#include "soci/blob.h"
#include "soci/type-wrappers.h"
#include "soci-exchange-cast.h"
#include <libpq/libpq-fs.h> // libpq
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <sstream>

#ifdef SOCI_POSTGRESQL_NOPARAMS
#ifndef SOCI_POSTGRESQL_NOBINDBYNAME
#define SOCI_POSTGRESQL_NOBINDBYNAME
#endif // SOCI_POSTGRESQL_NOBINDBYNAME
#endif // SOCI_POSTGRESQL_NOPARAMS

using namespace soci;
using namespace soci::details;
using namespace soci::details::postgresql;


void postgresql_standard_into_type_backend::define_by_pos(
    int & position, void * data, exchange_type type)
{
    data_ = data;
    type_ = type;
    position_ = position++;
}

void postgresql_standard_into_type_backend::pre_fetch()
{
    // nothing to do here
}

void postgresql_standard_into_type_backend::post_fetch(
    bool gotData, bool calledFromFetch, indicator * ind)
{
    if (calledFromFetch == true && gotData == false)
    {
        // this is a normal end-of-rowset condition,
        // no need to do anything (fetch() will return false)
        return;
    }

    if (gotData)
    {
        // postgresql_ positions start at 0
        int const pos = position_ - 1;

        // first, deal with indicators
        if (PQgetisnull(statement_.result_, statement_.currentRow_, pos) != 0)
        {
            if (ind == NULL)
            {
                throw soci_error(
                    "Null value fetched and no indicator defined.");
            }

            *ind = i_null;

            // no need to convert data if it is null
            return;
        }
        else
        {
            if (ind != NULL)
            {
                *ind = i_ok;
            }
        }

        // raw data, in text format
        char const * buf = PQgetvalue(statement_.result_,
            statement_.currentRow_, pos);

        switch (type_)
        {
        case x_char:
            exchange_type_cast<x_char>(data_) = *buf;
            break;
        case x_stdstring:
            exchange_type_cast<x_stdstring>(data_) = buf;
            break;
        case x_short:
            exchange_type_cast<x_short>(data_) = string_to_integer<short>(buf);
            break;
        case x_integer:
            exchange_type_cast<x_integer>(data_) = string_to_integer<int>(buf);
            break;
        case x_long_long:
            exchange_type_cast<x_long_long>(data_) = string_to_integer<long long>(buf);
            break;
        case x_unsigned_long_long:
            exchange_type_cast<x_unsigned_long_long>(data_) = string_to_unsigned_integer<unsigned long long>(buf);
            break;
        case x_double:
            exchange_type_cast<x_double>(data_) = cstring_to_double(buf);
            break;
        case x_stdtm:
            // attempt to parse the string and convert to std::tm
            parse_std_tm(buf, exchange_type_cast<x_stdtm>(data_));
            break;
        case x_rowid:
            {
                // RowID is internally identical to unsigned long

                rowid * rid = static_cast<rowid *>(data_);
                postgresql_rowid_backend * rbe
                    = static_cast<postgresql_rowid_backend *>(
                        rid->get_backend());

                rbe->value_ = string_to_unsigned_integer<unsigned long>(buf);
            }
            break;
        case x_blob:
            {
                unsigned long oid =
                    string_to_unsigned_integer<unsigned long>(buf);

                int fd = lo_open(statement_.session_.conn_, oid,
                    INV_READ | INV_WRITE);
                if (fd == -1)
                {
                    throw soci_error("Cannot open the blob object.");
                }

                blob * b = static_cast<blob *>(data_);
                postgresql_blob_backend * bbe
                     = static_cast<postgresql_blob_backend *>(b->get_backend());

                if (bbe->fd_ != -1)
                {
                    lo_close(statement_.session_.conn_, bbe->fd_);
                }

                bbe->fd_ = fd;
                bbe->oid_ = oid;
            }
            break;
        case x_xmltype:
            exchange_type_cast<x_xmltype>(data_).value.assign(buf);
            break;
        case x_longstring:
            exchange_type_cast<x_longstring>(data_).value.assign(buf);
            break;

        default:
            throw soci_error("Into element used with non-supported type.");
        }
    }
}

void postgresql_standard_into_type_backend::clean_up()
{
    // nothing to do here
}
