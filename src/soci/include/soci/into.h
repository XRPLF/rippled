//
// Copyright (C) 2004-2016 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_INTO_H_INCLUDED
#define SOCI_INTO_H_INCLUDED

#include "soci/into-type.h"
#include "soci/exchange-traits.h"
#include "soci/type-conversion.h"
// std
#include <cstddef>
#include <vector>

namespace soci
{

// the into function is a helper for defining output variables
// these helpers work with both basic and user-defined types thanks to
// the tag-dispatching, as defined in exchange_traits template

namespace details
{
template <typename T, typename Indicator>
struct into_container
{
    into_container(T &_t, Indicator &_ind)
        : t(_t), ind(_ind) {}

    T &t;
    Indicator &ind;
private:
    SOCI_NOT_ASSIGNABLE(into_container)
};

typedef void no_indicator;
template <typename T>
struct into_container<T, no_indicator>
{
    into_container(T &_t)
        : t(_t) {}

    T &t;
private:
    SOCI_NOT_ASSIGNABLE(into_container)
};

} // namespace details

template <typename T>
details::into_container<T, details::no_indicator>
    into(T &t)
{ return details::into_container<T, details::no_indicator>(t); }

template <typename T, typename Indicator>
details::into_container<T, Indicator>
    into(T &t, Indicator &ind)
{ return details::into_container<T, Indicator>(t, ind); }

// for char buffer with run-time size information
template <typename T>
details::into_type_ptr into(T & t, std::size_t bufSize)
{
    return details::into_type_ptr(new details::into_type<T>(t, bufSize));
}

// vectors with index ranges
    
template <typename T>
details::into_type_ptr into(std::vector<T> & t,
    std::size_t begin, std::size_t & end)
{
    return details::do_into(t, begin, &end,
        typename details::exchange_traits<std::vector<T> >::type_family());
}

template <typename T>
details::into_type_ptr into(std::vector<T> & t, std::vector<indicator> & ind,
    std::size_t begin, std::size_t & end)
{
    return details::do_into(t, ind, begin, &end,
        typename details::exchange_traits<std::vector<T> >::type_family());
}

} // namespace soci

#endif // SOCI_INTO_H_INCLUDED
