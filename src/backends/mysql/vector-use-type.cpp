//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// MySQL backend copyright (C) 2006 Pawel Aleksander Fedorynski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_MYSQL_SOURCE
#include "soci-mysql.h"
#include "common.h"
#include <soci-platform.h>
// std
#include <ciso646>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;
using namespace soci::details::mysql;


void mysql_vector_use_type_backend::bind_by_pos(int &position, void *data,
    exchange_type type)
{
    data_ = data;
    type_ = type;
    position_ = position++;
}

void mysql_vector_use_type_backend::bind_by_name(
    std::string const &name, void *data, exchange_type type)
{
    data_ = data;
    type_ = type;
    name_ = name;
}

void mysql_vector_use_type_backend::pre_use(indicator const *ind)
{
    std::size_t const vsize = size();
    for (size_t i = 0; i != vsize; ++i)
    {
        char *buf;

        // the data in vector can be either i_ok or i_null
        if (ind != NULL && ind[i] == i_null)
        {
            buf = new char[5];
            std::strcpy(buf, "NULL");
        }
        else
        {
            // allocate and fill the buffer with text-formatted client data
            switch (type_)
            {
            case x_char:
                {
                    std::vector<char> *pv
                        = static_cast<std::vector<char> *>(data_);
                    std::vector<char> &v = *pv;

                    char tmp[] = { v[i], '\0' };
                    buf = quote(statement_.session_.conn_, tmp, 1);
                }
                break;
            case x_stdstring:
                {
                    std::vector<std::string> *pv
                        = static_cast<std::vector<std::string> *>(data_);
                    std::vector<std::string> &v = *pv;

                    buf = quote(statement_.session_.conn_,
                        v[i].c_str(), v[i].size());
                }
                break;
            case x_short:
                {
                    std::vector<short> *pv
                        = static_cast<std::vector<short> *>(data_);
                    std::vector<short> &v = *pv;

                    std::size_t const bufSize
                        = std::numeric_limits<short>::digits10 + 3;
                    buf = new char[bufSize];
                    snprintf(buf, bufSize, "%d", static_cast<int>(v[i]));
                }
                break;
            case x_integer:
                {
                    std::vector<int> *pv
                        = static_cast<std::vector<int> *>(data_);
                    std::vector<int> &v = *pv;

                    std::size_t const bufSize
                        = std::numeric_limits<int>::digits10 + 3;
                    buf = new char[bufSize];
                    snprintf(buf, bufSize, "%d", v[i]);
                }
                break;
            case x_long_long:
                {
                    std::vector<long long> *pv
                        = static_cast<std::vector<long long> *>(data_);
                    std::vector<long long> &v = *pv;

                    std::size_t const bufSize
                        = std::numeric_limits<long long>::digits10 + 3;
                    buf = new char[bufSize];
                    snprintf(buf, bufSize, "%" LL_FMT_FLAGS "d", v[i]);
                }
                break;
            case x_unsigned_long_long:
                {
                    std::vector<unsigned long long> *pv
                        = static_cast<std::vector<unsigned long long> *>(data_);
                    std::vector<unsigned long long> &v = *pv;

                    std::size_t const bufSize
                        = std::numeric_limits<unsigned long long>::digits10 + 3;
                    buf = new char[bufSize];
                    snprintf(buf, bufSize, "%" LL_FMT_FLAGS "u", v[i]);
                }
                break;
            case x_double:
                {
                    std::vector<double> *pv
                        = static_cast<std::vector<double> *>(data_);
                    std::vector<double> &v = *pv;

                    if (is_infinity_or_nan(v[i])) {
                        throw soci_error(
                            "Use element used with infinity or NaN, which are "
                            "not supported by the MySQL server.");
                    }

                    std::size_t const bufSize = 100;
                    buf = new char[bufSize];

                    snprintf(buf, bufSize, "%.20g", v[i]);
                }
                break;
            case x_stdtm:
                {
                    std::vector<std::tm> *pv
                        = static_cast<std::vector<std::tm> *>(data_);
                    std::vector<std::tm> &v = *pv;

                    std::size_t const bufSize = 22;
                    buf = new char[bufSize];

                    snprintf(buf, bufSize, "\'%d-%02d-%02d %02d:%02d:%02d\'",
                        v[i].tm_year + 1900, v[i].tm_mon + 1, v[i].tm_mday,
                        v[i].tm_hour, v[i].tm_min, v[i].tm_sec);
                }
                break;

            default:
                throw soci_error(
                    "Use vector element used with non-supported type.");
            }
        }

        buffers_.push_back(buf);
    }

    if (position_ > 0)
    {
        // binding by position
        statement_.useByPosBuffers_[position_] = &buffers_[0];
    }
    else
    {
        // binding by name
        statement_.useByNameBuffers_[name_] = &buffers_[0];
    }
}

std::size_t mysql_vector_use_type_backend::size()
{
    std::size_t sz = 0; // dummy initialization to please the compiler
    switch (type_)
    {
        // simple cases
    case x_char:         sz = get_vector_size<char>         (data_); break;
    case x_short:        sz = get_vector_size<short>        (data_); break;
    case x_integer:      sz = get_vector_size<int>          (data_); break;
    case x_long_long:    sz = get_vector_size<long long>    (data_); break;
    case x_unsigned_long_long:
        sz = get_vector_size<unsigned long long>(data_);
        break;
    case x_double:       sz = get_vector_size<double>       (data_); break;
    case x_stdstring:    sz = get_vector_size<std::string>  (data_); break;
    case x_stdtm:        sz = get_vector_size<std::tm>      (data_); break;

    default:
        throw soci_error("Use vector element used with non-supported type.");
    }

    return sz;
}

void mysql_vector_use_type_backend::clean_up()
{
    std::size_t const bsize = buffers_.size();
    for (std::size_t i = 0; i != bsize; ++i)
    {
        delete [] buffers_[i];
    }
}
