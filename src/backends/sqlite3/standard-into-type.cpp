//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SQLITE3_SOURCE
#include "soci/soci-platform.h"
#include "soci/sqlite3/soci-sqlite3.h"
#include "soci/rowid.h"
#include "common.h"
#include "soci/blob.h"
#include "soci-cstrtod.h"
#include "soci-mktime.h"
#include "soci-exchange-cast.h"
// std
#include <cstdlib>
#include <ctime>
#include <string>

using namespace soci;
using namespace soci::details;
using namespace soci::details::sqlite3;

void sqlite3_standard_into_type_backend::define_by_pos(int & position, void * data,
                                                       exchange_type type)
{
    data_ = data;
    type_ = type;
    position_ = position++;
}

void sqlite3_standard_into_type_backend::pre_fetch()
{
    // ...
}

void sqlite3_standard_into_type_backend::post_fetch(bool gotData,
                                               bool calledFromFetch,
                                               indicator * ind)
{
    if (calledFromFetch == true && gotData == false)
    {
        // this is a normal end-of-rowset condition,
        // no need to do anything (fetch() will return false)
        return;
    }

    // sqlite columns start at 0
    int const pos = position_ - 1;

    if (gotData)
    {
        // first, deal with indicators
        if (sqlite3_column_type(statement_.stmt_, pos) == SQLITE_NULL)
        {
            if (ind == NULL)
            {
                throw soci_error(
                    "Null value fetched and no indicator defined.");
            }

            *ind = i_null;
            return;
        }
        else
        {
            if (ind != NULL)
            {
                *ind = i_ok;
            }
        }

        switch (type_)
        {
            case x_char:
            {
                const char *buf = reinterpret_cast<const char*>(
                    sqlite3_column_text(statement_.stmt_, pos)
                );
                const int bytes = sqlite3_column_bytes(statement_.stmt_, pos);
                exchange_type_cast<x_char>(data_) = (bytes > 0 ? buf[0] : '\0');
                break;
            }

            case x_stdstring:
            {
                const char *buf = reinterpret_cast<const char*>(
                    sqlite3_column_text(statement_.stmt_, pos)
                );
                const int bytes = sqlite3_column_bytes(statement_.stmt_, pos);
                exchange_type_traits<x_stdstring>::value_type &out
                    = exchange_type_cast<x_stdstring>(data_);
                out.assign(buf, bytes);
                break;
            }

            case x_short:
                exchange_type_cast<x_short>(data_)
                    = static_cast<exchange_type_traits<x_short>::value_type >(
                        sqlite3_column_int(statement_.stmt_, pos)
                    );
                break;

            case x_integer:
                exchange_type_cast<x_integer>(data_)
                    = static_cast<exchange_type_traits<x_integer>::value_type >(
                        sqlite3_column_int(statement_.stmt_, pos)
                    );
                break;

            case x_long_long:
                exchange_type_cast<x_long_long>(data_)
                    = static_cast<exchange_type_traits<x_long_long>::value_type >(
                        sqlite3_column_int64(statement_.stmt_, pos)
                    );
                break;

            case x_unsigned_long_long:
                exchange_type_cast<x_unsigned_long_long>(data_)
                    = static_cast<exchange_type_traits<x_unsigned_long_long>::value_type >(
                        sqlite3_column_int64(statement_.stmt_, pos)
                    );
                break;

            case x_double:
                exchange_type_cast<x_double>(data_)
                    = static_cast<exchange_type_traits<x_double>::value_type >(
                        sqlite3_column_double(statement_.stmt_, pos)
                    );
                break;

            case x_stdtm:
            {
                const char *buf = reinterpret_cast<const char*>(
                    sqlite3_column_text(statement_.stmt_, pos)
                );
                parse_std_tm((buf ? buf : ""), exchange_type_cast<x_stdtm>(data_));
                break;
            }

            case x_rowid:
            {
                // RowID is internally identical to unsigned long
                rowid *rid = static_cast<rowid *>(data_);
                sqlite3_rowid_backend *rbe = static_cast<sqlite3_rowid_backend *>(rid->get_backend());
                rbe->value_ = static_cast<unsigned long>(sqlite3_column_int64(statement_.stmt_, pos));
                break;
            }

            case x_blob:
            {
                blob *b = static_cast<blob *>(data_);
                sqlite3_blob_backend *bbe =
                    static_cast<sqlite3_blob_backend *>(b->get_backend());

                const char *buf
                    = reinterpret_cast<const char*>(
                        sqlite3_column_blob(statement_.stmt_, pos)
                    );

                int len = sqlite3_column_bytes(statement_.stmt_, pos);
                bbe->set_data(buf, len);
                break;
            }

            default:
                throw soci_error("Into element used with non-supported type.");
        }
    }
}

void sqlite3_standard_into_type_backend::clean_up()
{
    // ...
}
