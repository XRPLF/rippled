//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_EXCHANGE_TRAITS_H_INCLUDED
#define SOCI_EXCHANGE_TRAITS_H_INCLUDED

#include "soci/type-conversion-traits.h"
#include "soci/soci-backend.h"
#include "soci/type-wrappers.h"
// std
#include <ctime>
#include <string>
#include <vector>

namespace soci
{

namespace details
{

struct basic_type_tag {};
struct user_type_tag {};

template <typename T>
struct exchange_traits
{
    // this is used for tag-dispatch between implementations for basic types
    // and user-defined types
    typedef user_type_tag type_family;

    enum // anonymous
    {
        x_type =
            exchange_traits
            <
                typename type_conversion<T>::base_type
            >::x_type
    };
};

template <>
struct exchange_traits<short>
{
    typedef basic_type_tag type_family;
    enum { x_type = x_short };
};

template <>
struct exchange_traits<unsigned short> : exchange_traits<short>
{
};

template <>
struct exchange_traits<int>
{
    typedef basic_type_tag type_family;
    enum { x_type = x_integer };
};

template <>
struct exchange_traits<unsigned int> : exchange_traits<int>
{
};

template <>
struct exchange_traits<char>
{
    typedef basic_type_tag type_family;
    enum { x_type = x_char };
};

template <>
struct exchange_traits<long long>
{
    typedef basic_type_tag type_family;
    enum { x_type = x_long_long };
};

template <>
struct exchange_traits<unsigned long long>
{
    typedef basic_type_tag type_family;
    enum { x_type = x_unsigned_long_long };
};

// long must be mapped either to x_integer or x_long_long:
template<int long_size> struct long_traits_helper;
template<> struct long_traits_helper<4> { enum { x_type = x_integer }; };
template<> struct long_traits_helper<8> { enum { x_type = x_long_long }; };

template <>
struct exchange_traits<long int>
{
    typedef basic_type_tag type_family;
    enum { x_type = long_traits_helper<sizeof(long int)>::x_type };
};

template <>
struct exchange_traits<unsigned long> : exchange_traits<long>
{
};

template <>
struct exchange_traits<double>
{
    typedef basic_type_tag type_family;
    enum { x_type = x_double };
};

template <>
struct exchange_traits<std::string>
{
    typedef basic_type_tag type_family;
    enum { x_type = x_stdstring };
};

template <>
struct exchange_traits<std::tm>
{
    typedef basic_type_tag type_family;
    enum { x_type = x_stdtm };
};

template <typename T>
struct exchange_traits<std::vector<T> >
{
    typedef typename exchange_traits<T>::type_family type_family;
    enum { x_type = exchange_traits<T>::x_type };
};

// handling of wrapper types

template <>
struct exchange_traits<xml_type>
{
    typedef basic_type_tag type_family;
    enum { x_type = x_xmltype };
};

template <>
struct exchange_traits<long_string>
{
    typedef basic_type_tag type_family;
    enum { x_type = x_longstring };
};

} // namespace details

} // namespace soci

#endif // SOCI_EXCHANGE_TRAITS_H_INCLUDED
