//
// Copyright (C) 2004-2016 Maciej Sobczak, Stephen Hutton
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
#include "soci/type-wrappers.h"
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

using namespace soci;
using namespace soci::details;
using namespace soci::details::postgresql;


void postgresql_vector_into_type_backend::define_by_pos_bulk(
    int & position, void * data, exchange_type type,
    std::size_t begin, std::size_t * end)
{
    data_ = data;
    type_ = type;
    begin_ = begin;
    end_ = end;
    position_ = position++;

    end_var_ = full_size();
}

void postgresql_vector_into_type_backend::pre_fetch()
{
    // nothing to do here
}

namespace // anonymous
{

template <typename T>
void set_invector_(void * p, int indx, T const & val)
{
    std::vector<T> * dest =
        static_cast<std::vector<T> *>(p);

    std::vector<T> & v = *dest;
    v[indx] = val;
}

template <typename T, typename V>
void set_invector_wrappers_(void * p, int indx, V const & val)
{
    std::vector<T> * dest =
        static_cast<std::vector<T> *>(p);

    std::vector<T> & v = *dest;
    v[indx].value = val;
}

} // namespace anonymous

void postgresql_vector_into_type_backend::post_fetch(bool gotData, indicator * ind)
{
    if (gotData)
    {
        // Here, rowsToConsume_ in the Statement object designates
        // the number of rows that need to be put in the user's buffers.

        // postgresql_ column positions start at 0
        int const pos = position_ - 1;

        int const endRow = statement_.currentRow_ + statement_.rowsToConsume_;

        for (int curRow = statement_.currentRow_, i = begin_;
             curRow != endRow; ++curRow, ++i)
        {
            // first, deal with indicators
            if (PQgetisnull(statement_.result_, curRow, pos) != 0)
            {
                if (ind == NULL)
                {
                    throw soci_error(
                        "Null value fetched and no indicator defined.");
                }

                ind[i] = i_null;

                // no need to convert data if it is null, go to next row
                continue;
            }
            else
            {
                if (ind != NULL)
                {
                    ind[i] = i_ok;
                }
            }

            // buffer with data retrieved from server, in text format
            char * buf = PQgetvalue(statement_.result_, curRow, pos);

            switch (type_)
            {
            case x_char:
                set_invector_(data_, i, *buf);
                break;
            case x_stdstring:
                set_invector_<std::string>(data_, i, buf);
                break;
            case x_short:
                {
                    short const val = string_to_integer<short>(buf);
                    set_invector_(data_, i, val);
                }
                break;
            case x_integer:
                {
                    int const val = string_to_integer<int>(buf);
                    set_invector_(data_, i, val);
                }
                break;
            case x_long_long:
                {
                    long long const val = string_to_integer<long long>(buf);
                    set_invector_(data_, i, val);
                }
                break;
            case x_unsigned_long_long:
                {
                    unsigned long long const val =
                        string_to_unsigned_integer<unsigned long long>(buf);
                    set_invector_(data_, i, val);
                }
                break;
            case x_double:
                {
                    double const val = cstring_to_double(buf);
                    set_invector_(data_, i, val);
                }
                break;
            case x_stdtm:
                {
                    // attempt to parse the string and convert to std::tm
                    std::tm t = std::tm();
                    parse_std_tm(buf, t);

                    set_invector_(data_, i, t);
                }
                break;
            case x_xmltype:
                set_invector_wrappers_<xml_type, std::string>(data_, i, buf);
                break;
            case x_longstring:
                set_invector_wrappers_<long_string, std::string>(data_, i, buf);
                break;

            default:
                throw soci_error("Into element used with non-supported type.");
            }
        }
    }
    else // no data retrieved
    {
        // nothing to do, into vectors are already truncated
    }
}

namespace // anonymous
{

template <typename T>
void resizevector_(void * p, std::size_t sz)
{
    std::vector<T> * v = static_cast<std::vector<T> *>(p);
    v->resize(sz);
}

} // namespace anonymous

void postgresql_vector_into_type_backend::resize(std::size_t sz)
{
    if (user_ranges_)
    {
        // resize only in terms of user-provided ranges (below)
    }
    else
    {
        switch (type_)
        {
            // simple cases
        case x_char:
            resizevector_<char>(data_, sz);
            break;
        case x_short:
            resizevector_<short>(data_, sz);
            break;
        case x_integer:
            resizevector_<int>(data_, sz);
            break;
        case x_long_long:
            resizevector_<long long>(data_, sz);
            break;
        case x_unsigned_long_long:
            resizevector_<unsigned long long>(data_, sz);
            break;
        case x_double:
            resizevector_<double>(data_, sz);
            break;
        case x_stdstring:
            resizevector_<std::string>(data_, sz);
            break;
        case x_stdtm:
            resizevector_<std::tm>(data_, sz);
            break;
        case x_xmltype:
            resizevector_<xml_type>(data_, sz);
            break;
        case x_longstring:
            resizevector_<long_string>(data_, sz);
            break;
        default:
            throw soci_error("Into vector element used with non-supported type.");
        }

        end_var_ = sz;
    }

    // resize ranges, either user-provided or internally managed
    *end_ = begin_ + sz;
}

std::size_t postgresql_vector_into_type_backend::size()
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

std::size_t postgresql_vector_into_type_backend::full_size()
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
        throw soci_error("Into vector element used with non-supported type.");
    }

    return sz;
}

void postgresql_vector_into_type_backend::clean_up()
{
    // nothing to do here
}
