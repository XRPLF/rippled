//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SQLITE3_SOURCE
#include "soci-exchange-cast.h"
#include "soci/soci-platform.h"
#include "soci/sqlite3/soci-sqlite3.h"
#include "soci-dtocstr.h"
#include "common.h"
// std
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>


using namespace soci;
using namespace soci::details;
using namespace soci::details::sqlite3;

void sqlite3_vector_use_type_backend::bind_by_pos(int & position,
                                            void * data,
                                            exchange_type type)
{
    if (statement_.boundByName_)
    {
        throw soci_error(
         "Binding for use elements must be either by position or by name.");
    }

    data_ = data;
    type_ = type;
    position_ = position++;

    statement_.boundByPos_ = true;
}

void sqlite3_vector_use_type_backend::bind_by_name(std::string const & name,
                                             void * data,
                                             exchange_type type)
{
    if (statement_.boundByPos_)
    {
        throw soci_error(
         "Binding for use elements must be either by position or by name.");
    }

    data_ = data;
    type_ = type;
    name_ = ":" + name;

    statement_.reset_if_needed();
    position_ = sqlite3_bind_parameter_index(statement_.stmt_, name_.c_str());

    if (0 == position_)
    {
        std::ostringstream ss;
        ss << "Cannot bind (by name) to " << name_;
        throw soci_error(ss.str());
    }
    statement_.boundByName_ = true;
}

void sqlite3_vector_use_type_backend::pre_use(indicator const * ind)
{
    std::size_t const vsize = size();

    // make sure that useData can hold enough rows
    if (statement_.useData_.size() != vsize)
        statement_.useData_.resize(vsize);

    int const pos = position_ - 1;

    for (size_t i = 0; i != vsize; ++i)
    {
        // make sure that each row can accomodate the number of columns
        if (statement_.useData_[i].size() < static_cast<std::size_t>(position_))
            statement_.useData_[i].resize(position_);

        sqlite3_column &col = statement_.useData_[i][pos];

        // the data in vector can be either i_ok or i_null
        if (ind != NULL && ind[i] == i_null)
        {
            col.isNull_ = true;
            col.buffer_.data_ = NULL;
            continue;
        }

        col.isNull_ = false;

        switch (type_)
        {
            case x_char:
                col.type_ = dt_string;
                col.buffer_.constData_ = &(*static_cast<std::vector<exchange_type_traits<x_char>::value_type> *>(data_))[i];
                col.buffer_.size_ = 1;
                break;

            case x_stdstring:
            {
                std::string &s = (*static_cast<std::vector<exchange_type_traits<x_stdstring>::value_type> *>(data_))[i];
                col.type_ = dt_string;
                col.buffer_.constData_ = s.c_str();
                col.buffer_.size_ = s.size();
                break;
            }

            case x_short:
                col.type_ = dt_integer;
                col.int32_ = (*static_cast<std::vector<exchange_type_traits<x_short>::value_type> *>(data_))[i];
                break;

            case x_integer:
                col.type_ = dt_integer;
                col.int32_ = (*static_cast<std::vector<exchange_type_traits<x_integer>::value_type> *>(data_))[i];
                break;

            case x_long_long:
                col.type_ = dt_long_long;
                col.int64_ = (*static_cast<std::vector<exchange_type_traits<x_long_long>::value_type> *>(data_))[i];
                break;

            case x_unsigned_long_long:
                col.type_ = dt_long_long;
                col.int64_ = (*static_cast<std::vector<exchange_type_traits<x_unsigned_long_long>::value_type> *>(data_))[i];
                break;

            case x_double:
                col.type_ = dt_double;
                col.double_ = (*static_cast<std::vector<exchange_type_traits<x_double>::value_type> *>(data_))[i];
                break;

            case x_stdtm:
            {
                std::tm &tm = (*static_cast<std::vector<exchange_type_traits<x_stdtm>::value_type> *>(data_))[i];
                static const size_t bufSize = 20;

                col.type_ = dt_date;
                col.buffer_.data_ = new char[bufSize];
                col.buffer_.size_
                    = snprintf(col.buffer_.data_, bufSize, "%d-%02d-%02d %02d:%02d:%02d",
                        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                        tm.tm_hour, tm.tm_min, tm.tm_sec
                    );
                break;
            }

            default:
                throw soci_error(
                    "Use vector element used with non-supported type.");
        }
    }
}

std::size_t sqlite3_vector_use_type_backend::size()
{
    std::size_t sz = 0; // dummy initialization to please the compiler
    switch (type_)
    {
        // simple cases
    case x_char:
        sz = get_vector_size<char>(data_);
        break;
    case x_short:
        sz = get_vector_size<short>(data_);
        break;
    case x_integer:
        sz = get_vector_size<int>(data_);
        break;
    case x_long_long:
        sz = get_vector_size<long long>(data_);
        break;
    case x_unsigned_long_long:
        sz = get_vector_size<unsigned long long>(data_);
        break;
    case x_double:
        sz = get_vector_size<double>(data_);
        break;
    case x_stdstring:
        sz = get_vector_size<std::string>(data_);
        break;
    case x_stdtm:
        sz = get_vector_size<std::tm>(data_);
        break;
    default:
        throw soci_error("Use vector element used with non-supported type.");
    }

    return sz;
}

void sqlite3_vector_use_type_backend::clean_up()
{
    if (type_ != x_stdtm)
        return;

    int const pos = position_ - 1;

    for (sqlite3_recordset::iterator iter = statement_.useData_.begin(), last = statement_.useData_.end();
        iter != last; ++iter)
    {
        sqlite3_column &col = (*iter)[pos];

        if (col.isNull_)
            continue;

        delete[] col.buffer_.data_;
    }
}
