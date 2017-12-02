//
// Copyright (C) 2004-2016 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_USE_H_INCLUDED
#define SOCI_USE_H_INCLUDED

#include "soci/use-type.h"
#include "soci/exchange-traits.h"
#include "soci/type-conversion.h"
#include "soci/soci-backend.h"

namespace soci
{

namespace details
{
template <typename T, typename Indicator>
struct use_container
{
    use_container(T &_t, Indicator &_ind, const std::string &_name)
        : t(_t), ind(_ind), name(_name) {}

    T &t;
    Indicator &ind;
    const std::string &name;
private:
    SOCI_NOT_ASSIGNABLE(use_container)
};

typedef void no_indicator;
template <typename T>
struct use_container<T, no_indicator>
{
    use_container(T &_t, const std::string &_name)
        : t(_t), name(_name) {}

    T &t;
    const std::string &name;
private:
    SOCI_NOT_ASSIGNABLE(use_container)
};

} // namespace details

template <typename T>
details::use_container<T, details::no_indicator> use(T &t, const std::string &name = std::string())
{ return details::use_container<T, details::no_indicator>(t, name); }

template <typename T>
details::use_container<const T, details::no_indicator> use(T const &t, const std::string &name = std::string())
{ return details::use_container<const T, details::no_indicator>(t, name); }

template <typename T>
details::use_container<T, indicator> use(T &t, indicator & ind, std::string const &name = std::string())
{ return details::use_container<T, indicator>(t, ind, name); }

template <typename T>
details::use_container<const T, indicator> use(T const &t, indicator & ind, std::string const &name = std::string())
{ return details::use_container<const T, indicator>(t, ind, name); }

// vector containers
template <typename T>
details::use_container<T, std::vector<indicator> >
    use(T &t, std::vector<indicator> & ind, const std::string &name = std::string())
{ return details::use_container<T, std::vector<indicator> >(t, ind, name); }

template <typename T>
details::use_container<std::vector<T>, details::no_indicator >
    use(std::vector<T> &t, const std::string &name = std::string())
{ return details::use_container<std::vector<T>, details::no_indicator>(t, name); }

// vectors with index ranges

template <typename T>
details::use_type_ptr use(std::vector<T> & t,
    std::size_t begin, std::size_t & end,
    const std::string &name = std::string())
{
    return details::do_use(t, begin, &end, name,
        typename details::exchange_traits<std::vector<T> >::type_family());
}

template <typename T>
details::use_type_ptr use(const std::vector<T> & t,
    std::size_t begin, std::size_t & end,
    const std::string &name = std::string())
{
    return details::do_use(t, begin, &end, name,
        typename details::exchange_traits<std::vector<T> >::type_family());
}

template <typename T>
details::use_type_ptr use(std::vector<T> & t, std::vector<indicator> & ind,
    std::size_t begin, std::size_t & end,
    const std::string &name = std::string())
{
    return details::do_use(t, ind, begin, &end, name,
        typename details::exchange_traits<std::vector<T> >::type_family());
}

template <typename T>
details::use_type_ptr use(const std::vector<T> & t, std::vector<indicator> & ind,
    std::size_t begin, std::size_t & end,
    const std::string &name = std::string())
{
    return details::do_use(t, ind, begin, &end, name,
        typename details::exchange_traits<std::vector<T> >::type_family());
}

} // namespace soci

#endif // SOCI_USE_H_INCLUDED
