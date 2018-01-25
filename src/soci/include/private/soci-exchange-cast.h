//
// Copyright (C) 2015 Vadim Zeitlin
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_EXCHANGE_CAST_H_INCLUDED
#define SOCI_EXCHANGE_CAST_H_INCLUDED

#include "soci/soci-backend.h"
#include "soci/type-wrappers.h"

#include <ctime>

namespace soci
{

namespace details
{

// cast the given non-null untyped pointer to its corresponding type
template <exchange_type e> struct exchange_type_traits;

template <>
struct exchange_type_traits<x_char>
{
  typedef char value_type;
};

template <>
struct exchange_type_traits<x_stdstring>
{
  typedef std::string value_type;
};

template <>
struct exchange_type_traits<x_short>
{
  typedef short value_type;
};

template <>
struct exchange_type_traits<x_integer>
{
  typedef int value_type;
};

template <>
struct exchange_type_traits<x_long_long>
{
  typedef long long value_type;
};

template <>
struct exchange_type_traits<x_unsigned_long_long>
{
  typedef unsigned long long value_type;
};

template <>
struct exchange_type_traits<x_double>
{
  typedef double value_type;
};

template <>
struct exchange_type_traits<x_stdtm>
{
  typedef std::tm value_type;
};

template <>
struct exchange_type_traits<x_longstring>
{
  typedef long_string value_type;
};

template <>
struct exchange_type_traits<x_xmltype>
{
  typedef xml_type value_type;
};

// exchange_type_traits not defined for x_statement, x_rowid and x_blob here.

template <exchange_type e>
typename exchange_type_traits<e>::value_type& exchange_type_cast(void *data)
{
    return *static_cast<typename exchange_type_traits<e>::value_type*>(data);
}

} // namespace details

} // namespace soci

#endif // SOCI_EXCHANGE_CAST_H_INCLUDED
