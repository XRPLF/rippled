//
// Copyright (C) 2010 Maciej Sobczak
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_UNSIGNED_TYPES_H_INCLUDED
#define SOCI_UNSIGNED_TYPES_H_INCLUDED

#include "soci/type-conversion-traits.h"
#include <limits>

namespace soci
{

// simple fall-back for unsigned types

template <>
struct type_conversion<unsigned char>
{
    typedef long long base_type;

    static void from_base(base_type const & in, indicator ind,
        unsigned char & out)
    {
        if (ind == i_null)
        {
            throw soci_error("Null value not allowed for this type.");
        }

        const base_type max = (std::numeric_limits<unsigned char>::max)();
        const base_type min = (std::numeric_limits<unsigned char>::min)();
        if (in < min || in > max)
        {
            throw soci_error("Value outside of allowed range.");
        }

        out = static_cast<unsigned char>(in);
    }

    static void to_base(unsigned char const & in,
        base_type & out, indicator & ind)
    {
        out = static_cast<base_type>(in);
        ind = i_ok;
    }
};

template <>
struct type_conversion<unsigned short>
{
    typedef long long base_type;

    static void from_base(base_type const & in, indicator ind,
        unsigned short & out)
    {
        if (ind == i_null)
        {
            throw soci_error("Null value not allowed for this type.");
        }

        const long long max = (std::numeric_limits<unsigned short>::max)();
        const long long min = (std::numeric_limits<unsigned short>::min)();
        if (in < min || in > max)
        {
            throw soci_error("Value outside of allowed range.");
        }

        out = static_cast<unsigned short>(in);
    }

    static void to_base(unsigned short const & in,
        base_type & out, indicator & ind)
    {
        out = static_cast<base_type>(in);
        ind = i_ok;
    }
};

template <>
struct type_conversion<unsigned int>
{
    typedef long long base_type;

    static void from_base(base_type const & in, indicator ind,
        unsigned int & out)
    {
        if (ind == i_null)
        {
            throw soci_error("Null value not allowed for this type.");
        }

        const long long max = (std::numeric_limits<unsigned int>::max)();
        const long long min = (std::numeric_limits<unsigned int>::min)();
        if (in < min || in > max)
        {
            throw soci_error("Value outside of allowed range.");
        }

        out = static_cast<unsigned int>(in);
    }

    static void to_base(unsigned int const & in,
        base_type & out, indicator & ind)
    {
        out = static_cast<base_type>(in);
        ind = i_ok;
    }
};

} // namespace soci

#endif // SOCI_UNSIGNED_TYPES_H_INCLUDED
