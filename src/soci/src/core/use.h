//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_USE_H_INCLUDED
#define SOCI_USE_H_INCLUDED

#include "use-type.h"
#include "exchange-traits.h"
#include "type-conversion.h"

namespace soci
{

// the use function is a helper for defining input variables
// these helpers work with both basic and user-defined types thanks to
// the tag-dispatching, as defined in exchange_traits template

template <typename T>
details::use_type_ptr use(T & t, std::string const & name = std::string())
{
    return details::do_use(t, name,
        typename details::exchange_traits<T>::type_family());
}

template <typename T>
details::use_type_ptr use(T const & t,
    std::string const & name = std::string())
{
    return details::do_use(t, name,
        typename details::exchange_traits<T>::type_family());
}

template <typename T>
details::use_type_ptr use(T & t, indicator & ind,
    std::string const &name = std::string())
{
    return details::do_use(t, ind, name,
        typename details::exchange_traits<T>::type_family());
}

template <typename T>
details::use_type_ptr use(T const & t, indicator & ind,
    std::string const &name = std::string())
{
    return details::do_use(t, ind, name,
        typename details::exchange_traits<T>::type_family());
}

template <typename T>
details::use_type_ptr use(T & t, std::vector<indicator> & ind,
    std::string const & name = std::string())
{
    return details::do_use(t, ind, name,
        typename details::exchange_traits<T>::type_family());
}

template <typename T>
details::use_type_ptr use(T const & t, std::vector<indicator> & ind,
    std::string const & name = std::string())
{
    return details::do_use(t, ind, name,
        typename details::exchange_traits<T>::type_family());
}

// for char buffer with run-time size information
template <typename T>
details::use_type_ptr use(T & t, std::size_t bufSize,
    std::string const & name = std::string())
{
    return details::use_type_ptr(new details::use_type<T>(t, bufSize));
}

// for char buffer with run-time size information
template <typename T>
details::use_type_ptr use(T const & t, std::size_t bufSize,
    std::string const & name = std::string())
{
    return details::use_type_ptr(new details::use_type<T>(t, bufSize));
}

} // namespace soci

#endif // SOCI_USE_H_INCLUDED
