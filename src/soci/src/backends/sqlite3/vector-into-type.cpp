//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include <soci-platform.h>
#include "soci-sqlite3.h"
#include "common.h"
// std
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

using namespace soci;
using namespace soci::details;
using namespace soci::details::sqlite3;

void sqlite3_vector_into_type_backend::define_by_pos(
    int& position, void* data, exchange_type type)
{
    data_ = data;
    type_ = type;
    position_ = position++;
}

void sqlite3_vector_into_type_backend::pre_fetch()
{
    // ...
}

namespace // anonymous
{

template <typename T>
void set_in_vector(void* p, int indx, T const& val)
{
    assert(NULL != p);

    std::vector<T>* dest = static_cast<std::vector<T>*>(p);
    std::vector<T>& v = *dest;
    v[indx] = val;
}

} // namespace anonymous

void sqlite3_vector_into_type_backend::post_fetch(bool gotData, indicator * ind)
{
    if (!gotData)
    {
        // no data retrieved
        return;
    }

    int const endRow = static_cast<int>(statement_.dataCache_.size());
    for (int i = 0; i < endRow; ++i)
    {
        sqlite3_column const& curCol =
            statement_.dataCache_[i][position_-1];

        if (curCol.isNull_)
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

        const char * buf = curCol.data_.c_str();

        // set buf to a null string if a null pointer is returned
        if (buf == NULL)
        {
            buf = "";
        }

        switch (type_)
        {
        case x_char:
            set_in_vector(data_, i, *buf);
            break;
        case x_stdstring:
            set_in_vector<std::string>(data_, i, buf);
            break;
        case x_short:
            {
                short const val = string_to_integer<short>(buf);
                set_in_vector(data_, i, val);
            }
            break;
        case x_integer:
            {
                int const val = string_to_integer<int>(buf);
                set_in_vector(data_, i, val);
            }
            break;
        case x_long_long:
            {
                long long const val = string_to_integer<long long>(buf);
                set_in_vector(data_, i, val);
            }
            break;
        case x_unsigned_long_long:
            {
                unsigned long long const val
                    = string_to_unsigned_integer<unsigned long long>(buf);
                set_in_vector(data_, i, val);
            }
            break;
        case x_double:
            {
                double const val = strtod(buf, NULL);
                set_in_vector(data_, i, val);
            }
            break;
        case x_stdtm:
            {
                // attempt to parse the string and convert to std::tm
                std::tm t;
                parse_std_tm(buf, t);

                set_in_vector(data_, i, t);
            }
            break;
        default:
            throw soci_error("Into element used with non-supported type.");
        }
    }
}

void sqlite3_vector_into_type_backend::resize(std::size_t sz)
{
    switch (type_)
    {
        // simple cases
    case x_char:
        resize_vector<char>(data_, sz);
        break;
    case x_short:
        resize_vector<short>(data_, sz);
        break;
    case x_integer:
        resize_vector<int>(data_, sz);
        break;
    case x_long_long:
        resize_vector<long long>(data_, sz);
        break;
    case x_unsigned_long_long:
        resize_vector<unsigned long long>(data_, sz);
        break;
    case x_double:
        resize_vector<double>(data_, sz);
        break;
    case x_stdstring:
        resize_vector<std::string>(data_, sz);
        break;
    case x_stdtm:
        resize_vector<std::tm>(data_, sz);
        break;
    default:
        throw soci_error("Into vector element used with non-supported type.");
    }
}

std::size_t sqlite3_vector_into_type_backend::size()
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
        throw soci_error("Into vector element used with non-supported type.");
    }

    return sz;
}

void sqlite3_vector_into_type_backend::clean_up()
{
    // ...
}
