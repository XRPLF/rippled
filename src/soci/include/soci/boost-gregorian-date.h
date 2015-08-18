//
// Copyright (C) 2008 Maciej Sobczak
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_BOOST_GREGORIAN_DATE_H_INCLUDED
#define SOCI_BOOST_GREGORIAN_DATE_H_INCLUDED

#include "soci/type-conversion-traits.h"
// boost
#include <boost/date_time/gregorian/gregorian_types.hpp>
#include <boost/date_time/gregorian/conversion.hpp>
// std
#include <ctime>

namespace soci
{

template<>
struct type_conversion<boost::gregorian::date>
{
    typedef std::tm base_type;

    static void from_base(
        base_type const & in, indicator ind, boost::gregorian::date & out)
    {
        if (ind == i_null)
        {
            throw soci_error("Null value not allowed for this type");
        }

        out = boost::gregorian::date_from_tm(in);
    }

    static void to_base(
        boost::gregorian::date const & in, base_type & out, indicator & ind)
    {
        out = boost::gregorian::to_tm(in);
        ind = i_ok;
    }
};

} // namespace soci

#endif // SOCI_BOOST_GREGORIAN_DATE_H_INCLUDED
