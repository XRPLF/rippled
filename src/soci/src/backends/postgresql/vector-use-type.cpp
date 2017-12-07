//
// Copyright (C) 2004-2016 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_POSTGRESQL_SOURCE
#include "soci/soci-platform.h"
#include "soci/postgresql/soci-postgresql.h"
#include "soci-dtocstr.h"
#include "common.h"
#include "soci/type-wrappers.h"
#include <libpq/libpq-fs.h> // libpq
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
#include <sstream>

#ifdef SOCI_POSTGRESQL_NOPARAMS
#ifndef SOCI_POSTGRESQL_NOBINDBYNAME
#define SOCI_POSTGRESQL_NOBINDBYNAME
#endif // SOCI_POSTGRESQL_NOBINDBYNAME
#endif // SOCI_POSTGRESQL_NOPARAMS


using namespace soci;
using namespace soci::details;
using namespace soci::details::postgresql;


void postgresql_vector_use_type_backend::bind_by_pos_bulk(int & position,
    void * data, exchange_type type,
    std::size_t begin, std::size_t * end)
{
    data_ = data;
    type_ = type;
    begin_ = begin;
    end_ = end;
    position_ = position++;

    end_var_ = full_size();
}

void postgresql_vector_use_type_backend::bind_by_name_bulk(
    std::string const & name, void * data, exchange_type type,
    std::size_t begin, std::size_t * end)
{
    data_ = data;
    type_ = type;
    begin_ = begin;
    end_ = end;
    name_ = name;

    end_var_ = full_size();
}

void postgresql_vector_use_type_backend::pre_use(indicator const * ind)
{
    std::size_t vend;

    if (end_ != NULL && *end_ != 0)
    {
        vend = *end_;
    }
    else
    {
        vend = end_var_;
    }
    
    for (size_t i = begin_; i != vend; ++i)
    {
        char * buf;

        // the data in vector can be either i_ok or i_null
        if (ind != NULL && ind[i] == i_null)
        {
            buf = NULL;
        }
        else
        {
            // allocate and fill the buffer with text-formatted client data
            switch (type_)
            {
            case x_char:
                {
                    std::vector<char> * pv
                        = static_cast<std::vector<char> *>(data_);
                    std::vector<char> & v = *pv;

                    buf = new char[2];
                    buf[0] = v[i];
                    buf[1] = '\0';
                }
                break;
            case x_stdstring:
                {
                    std::vector<std::string> * pv
                        = static_cast<std::vector<std::string> *>(data_);
                    std::vector<std::string> & v = *pv;

                    buf = new char[v[i].size() + 1];
                    std::strcpy(buf, v[i].c_str());
                }
                break;
            case x_short:
                {
                    std::vector<short> * pv
                        = static_cast<std::vector<short> *>(data_);
                    std::vector<short> & v = *pv;

                    std::size_t const bufSize
                        = std::numeric_limits<short>::digits10 + 3;
                    buf = new char[bufSize];
                    snprintf(buf, bufSize, "%d", static_cast<int>(v[i]));
                }
                break;
            case x_integer:
                {
                    std::vector<int> * pv
                        = static_cast<std::vector<int> *>(data_);
                    std::vector<int> & v = *pv;

                    std::size_t const bufSize
                        = std::numeric_limits<int>::digits10 + 3;
                    buf = new char[bufSize];
                    snprintf(buf, bufSize, "%d", v[i]);
                }
                break;
            case x_long_long:
                {
                    std::vector<long long>* pv
                        = static_cast<std::vector<long long>*>(data_);
                    std::vector<long long>& v = *pv;

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
                    std::vector<double> * pv
                        = static_cast<std::vector<double> *>(data_);
                    std::vector<double> & v = *pv;

                    std::string const s = double_to_cstring(v[i]);

                    buf = new char[s.size() + 1];
                    std::strcpy(buf, s.c_str());
                }
                break;
            case x_stdtm:
                {
                    std::vector<std::tm> * pv
                        = static_cast<std::vector<std::tm> *>(data_);
                    std::vector<std::tm> & v = *pv;

                    std::size_t const bufSize = 20;
                    buf = new char[bufSize];

                    snprintf(buf, bufSize, "%d-%02d-%02d %02d:%02d:%02d",
                        v[i].tm_year + 1900, v[i].tm_mon + 1, v[i].tm_mday,
                        v[i].tm_hour, v[i].tm_min, v[i].tm_sec);
                }
                break;
            case x_xmltype:
                {
                    std::vector<xml_type> * pv
                        = static_cast<std::vector<xml_type> *>(data_);
                    std::vector<xml_type> & v = *pv;

                    buf = new char[v[i].value.size() + 1];
                    std::strcpy(buf, v[i].value.c_str());
                }
                break;
            case x_longstring:
                {
                    std::vector<long_string> * pv
                        = static_cast<std::vector<long_string> *>(data_);
                    std::vector<long_string> & v = *pv;

                    buf = new char[v[i].value.size() + 1];
                    std::strcpy(buf, v[i].value.c_str());
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

std::size_t postgresql_vector_use_type_backend::size()
{
    // as a special error-detection measure, check if the actual vector size
    // was changed since the original bind (when it was stored in end_var_):
    const std::size_t actual_size = full_size();
    if (actual_size != end_var_)
    {
        // ... and in that case return the actual size
        return actual_size;
    }
    
    if (end_ != NULL && *end_ != 0)
    {
        return *end_ - begin_;
    }
    else
    {
        return end_var_;
    }
}

std::size_t postgresql_vector_use_type_backend::full_size()
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
    case x_xmltype:
        sz = get_vector_size<xml_type>(data_);
        break;
    case x_longstring:
        sz = get_vector_size<long_string>(data_);
        break;
    default:
        throw soci_error("Use vector element used with non-supported type.");
    }

    return sz;
}

void postgresql_vector_use_type_backend::clean_up()
{
    std::size_t const bsize = buffers_.size();
    for (std::size_t i = 0; i != bsize; ++i)
    {
        delete [] buffers_[i];
    }
}
