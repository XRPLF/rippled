//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// MySQL backend copyright (C) 2006 Pawel Aleksander Fedorynski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_MYSQL_SOURCE
#include "soci/mysql/soci-mysql.h"
#include "soci/soci-platform.h"
#include "common.h"
#include "soci-exchange-cast.h"
#include "soci-mktime.h"
// std
#include <ciso646>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;
using namespace soci::details::mysql;


void mysql_standard_into_type_backend::define_by_pos(
    int &position, void *data, exchange_type type)
{
    data_ = data;
    type_ = type;
    position_ = position++;
}

void mysql_standard_into_type_backend::pre_fetch()
{
    // nothing to do here
}

void mysql_standard_into_type_backend::post_fetch(
    bool gotData, bool calledFromFetch, indicator *ind)
{
    if (calledFromFetch == true && gotData == false)
    {
        // this is a normal end-of-rowset condition,
        // no need to do anything (fetch() will return false)
        return;
    }

    if (gotData)
    {
        int pos = position_ - 1;
        //mysql_data_seek(statement_.result_, statement_.currentRow_);
        mysql_row_seek(statement_.result_,
            statement_.resultRowOffsets_[statement_.currentRow_]);
        MYSQL_ROW row = mysql_fetch_row(statement_.result_);
        if (row[pos] == NULL)
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
        const char *buf = row[pos] != NULL ? row[pos] : "";
        switch (type_)
        {
        case x_char:
            exchange_type_cast<x_char>(data_) = *buf;
            break;
        case x_stdstring:
            {
                std::string& dest = exchange_type_cast<x_stdstring>(data_);
                unsigned long * lengths =
                    mysql_fetch_lengths(statement_.result_);
                dest.assign(buf, lengths[pos]);
            }
            break;
        case x_short:
            parse_num(buf, exchange_type_cast<x_short>(data_));
            break;
        case x_integer:
            parse_num(buf, exchange_type_cast<x_integer>(data_));
            break;
        case x_long_long:
            parse_num(buf, exchange_type_cast<x_long_long>(data_));
            break;
        case x_unsigned_long_long:
            parse_num(buf, exchange_type_cast<x_unsigned_long_long>(data_));
            break;
        case x_double:
            parse_num(buf, exchange_type_cast<x_double>(data_));
            break;
        case x_stdtm:
            // attempt to parse the string and convert to std::tm
            parse_std_tm(buf, exchange_type_cast<x_stdtm>(data_));
            break;
        default:
            throw soci_error("Into element used with non-supported type.");
        }
    }
}

void mysql_standard_into_type_backend::clean_up()
{
    // nothing to do here
}
