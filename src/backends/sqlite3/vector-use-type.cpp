//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include "soci-sqlite3.h"
#include <soci-platform.h>
#include "common.h"
// std
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>

#ifdef _MSC_VER
#pragma warning(disable:4355 4996)
#define snprintf _snprintf // TODO: use soci-platform.h
#endif

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
    {
        statement_.useData_.resize(vsize);
    }

    int const pos = position_ - 1;

    for (size_t i = 0; i != vsize; ++i)
    {
        char *buf = 0;

        // make sure that each row can accomodate the number of columns
        if (statement_.useData_[i].size() < static_cast<std::size_t>(position_))
        {
            statement_.useData_[i].resize(position_);
        }

        // the data in vector can be either i_ok or i_null
        if (ind != NULL && ind[i] == i_null)
        {
            statement_.useData_[i][pos].isNull_ = true;
            statement_.useData_[i][pos].data_ = "";
            statement_.useData_[i][pos].blobBuf_ = 0;
            statement_.useData_[i][pos].blobSize_ = 0;
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

                    buf = new char[2];
                    buf[0] = v[i];
                    buf[1] = '\0';
                }
                break;
            case x_stdstring:
                {
                    std::vector<std::string> *pv
                        = static_cast<std::vector<std::string> *>(data_);
                    std::vector<std::string> &v = *pv;

                    buf = new char[v[i].size() + 1];
                    std::strcpy(buf, v[i].c_str());
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
                    std::vector<unsigned long long>* pv
                        = static_cast<std::vector<unsigned long long>*>(data_);
                    std::vector<unsigned long long>& v = *pv;

                    std::size_t const bufSize
                        = std::numeric_limits<unsigned long long>::digits10 + 2;
                    buf = new char[bufSize];
                    snprintf(buf, bufSize, "%" LL_FMT_FLAGS "u", v[i]);
                }
                break;
            case x_double:
                {
                    // no need to overengineer it (KISS)...

                    std::vector<double> *pv
                        = static_cast<std::vector<double> *>(data_);
                    std::vector<double> &v = *pv;

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

                    std::size_t const bufSize = 20;
                    buf = new char[bufSize];

                    snprintf(buf, bufSize, "%d-%02d-%02d %02d:%02d:%02d",
                        v[i].tm_year + 1900, v[i].tm_mon + 1, v[i].tm_mday,
                        v[i].tm_hour, v[i].tm_min, v[i].tm_sec);
                }
                break;
            default:
                throw soci_error(
                    "Use vector element used with non-supported type.");
            }

            statement_.useData_[i][pos].isNull_ = false;
            statement_.useData_[i][pos].data_ = buf;
            statement_.useData_[i][pos].blobBuf_ = 0;
            statement_.useData_[i][pos].blobSize_ = 0;
        }

        if (buf)
        {
            delete [] buf;
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
    // ...
}
