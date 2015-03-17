//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, Rafal Bobrowski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include "common.h"
#include <soci-backend.h>
#include <ibase.h> // FireBird
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <iostream>
#include <string>

namespace soci
{

namespace details
{

namespace firebird
{

char * allocBuffer(XSQLVAR* var)
{
    std::size_t size;
    int type = var->sqltype & ~1;
    if (type == SQL_VARYING)
    {
        size = var->sqllen + sizeof(short);
    }
    else if (type == SQL_TIMESTAMP || type == SQL_TYPE_TIME
            || type == SQL_TYPE_DATE)
    {
        size = sizeof(std::tm);
    }
    else
    {
        size = var->sqllen;
    }

    return new char[size];
}

void tmEncode(short type, std::tm * src, void * dst)
{
    switch (type & ~1)
    {
        // In Interbase v6 DATE represents a date-only data type,
        // in InterBase v5 DATE represents a date+time data type.
    case SQL_TIMESTAMP:
        isc_encode_timestamp(src, static_cast<ISC_TIMESTAMP*>(dst));
        break;
    case SQL_TYPE_TIME:
        isc_encode_sql_time(src, static_cast<ISC_TIME*>(dst));
        break;
    case SQL_TYPE_DATE:
        isc_encode_sql_date(src, static_cast<ISC_DATE*>(dst));
        break;
    default:
        std::ostringstream msg;
        msg << "Unexpected type of date/time field (" << type << ")";
        throw soci_error(msg.str());
    }
}

void tmDecode(short type, void * src, std::tm * dst)
{
    switch (type & ~1)
    {
    case SQL_TIMESTAMP:
        isc_decode_timestamp(static_cast<ISC_TIMESTAMP*>(src), dst);
        break;
    case SQL_TYPE_TIME:
        isc_decode_sql_time(static_cast<ISC_TIME*>(src), dst);
        break;
    case SQL_TYPE_DATE:
        isc_decode_sql_date(static_cast<ISC_DATE*>(src), dst);
        break;
    default:
        std::ostringstream msg;
        msg << "Unexpected type of date/time field (" << type << ")";
        throw soci_error(msg.str());
    }
}

void setTextParam(char const * s, std::size_t size, char * buf_,
    XSQLVAR * var)
{
    //std::cerr << "setTextParam: var->sqltype=" << var->sqltype << std::endl;
    short sz = 0;
    if (size < static_cast<std::size_t>(var->sqllen))
    {
        sz = static_cast<short>(size);
    }
    else
    {
        sz = var->sqllen;
    }

    if ((var->sqltype & ~1) == SQL_VARYING)
    {
        std::memcpy(buf_, &sz, sizeof(short));
        std::memcpy(buf_ + sizeof(short), s, sz);
    }
    else if ((var->sqltype & ~1) == SQL_TEXT)
    {
        std::memcpy(buf_, s, sz);
        if (sz < var->sqllen)
        {
            std::memset(buf_+sz, ' ', var->sqllen - sz);
        }
    }
    else if ((var->sqltype & ~1) == SQL_SHORT)
    {
        parse_decimal<short, unsigned short>(buf_, var, s);
    }
    else if ((var->sqltype & ~1) == SQL_LONG)
    {
        parse_decimal<int, unsigned int>(buf_, var, s);
    }
    else if ((var->sqltype & ~1) == SQL_INT64)
    {
        parse_decimal<long long, unsigned long long>(buf_, var, s);
    }
    else if ((var->sqltype & ~1) == SQL_TIMESTAMP
            || (var->sqltype & ~1) == SQL_TYPE_DATE)
    {
        unsigned short year, month, day, hour, min, sec;
        if (std::sscanf(s, "%hu-%hu-%hu %hu:%hu:%hu",
                    &year, &month, &day, &hour, &min, &sec) != 6)
        {
            if (std::sscanf(s, "%hu-%hu-%huT%hu:%hu:%hu",
                        &year, &month, &day, &hour, &min, &sec) != 6)
            {
                hour = min = sec = 0;
                if (std::sscanf(s, "%hu-%hu-%hu", &year, &month, &day) != 3)
                {
                    throw soci_error("Could not parse timestamp value.");
                }
            }
        }
        std::tm t;
        std::memset(&t, 0, sizeof(t));
        t.tm_year = year - 1900;
        t.tm_mon = month - 1;
        t.tm_mday = day;
        t.tm_hour = hour;
        t.tm_min = min;
        t.tm_sec = sec;
        std::memcpy(buf_, &t, sizeof(t));
        tmEncode(var->sqltype, &t, buf_);
    }
    else if ((var->sqltype & ~1) == SQL_TYPE_TIME)
    {
        unsigned short hour, min, sec;
        if (std::sscanf(s, "%hu:%hu:%hu", &hour, &min, &sec) != 3)
        {
            throw soci_error("Could not parse timestamp value.");
        }
        std::tm t;
        std::memset(&t, 0, sizeof(t));
        t.tm_hour = hour;
        t.tm_min = min;
        t.tm_sec = sec;
        std::memcpy(buf_, &t, sizeof(t));
        tmEncode(var->sqltype, &t, buf_);
    }
    else
    {
        throw soci_error("Unexpected string type.");
    }
}

std::string getTextParam(XSQLVAR const *var)
{
    //std::cerr << "getTextParam: var->sqltype=" << var->sqltype << std::endl;
    short size;
    std::size_t offset = 0;

    if ((var->sqltype & ~1) == SQL_VARYING)
    {
        size = *reinterpret_cast<short*>(var->sqldata);
        offset = sizeof(short);
    }
    else if ((var->sqltype & ~1) == SQL_TEXT)
    {
        size = var->sqllen;
    }
    else if ((var->sqltype & ~1) == SQL_SHORT)
    {
        return format_decimal<short>(var->sqldata, var->sqlscale);
    }
    else if ((var->sqltype & ~1) == SQL_LONG)
    {
        return format_decimal<int>(var->sqldata, var->sqlscale);
    }
    else if ((var->sqltype & ~1) == SQL_INT64)
    {
        return format_decimal<long long>(var->sqldata, var->sqlscale);
    }
    else
        throw soci_error("Unexpected string type");

    return std::string(var->sqldata + offset, size);
}

} // namespace firebird

} // namespace details

} // namespace soci
