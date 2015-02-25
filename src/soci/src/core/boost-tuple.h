//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_BOOST_TUPLE_H_INCLUDED
#define SOCI_BOOST_TUPLE_H_INCLUDED

#include "values.h"
#include "type-conversion-traits.h"
// boost
#include <boost/tuple/tuple.hpp>

#if defined(BOOST_VERSION) && BOOST_VERSION < 103500

namespace soci
{

template <typename T0>
struct type_conversion<boost::tuple<T0> >
{
    typedef values base_type;

    static void from_base(base_type const & in, indicator ind,
        boost::tuple<T0> & out)
    {
        in
            >> boost::tuples::get<0>(out);
    }

    static void to_base(boost::tuple<T0> & in,
        base_type & out, indicator & ind)
    {
        out
            << boost::tuples::get<0>(in);
    }
};

template <typename T0, typename T1>
struct type_conversion<boost::tuple<T0, T1> >
{
    typedef values base_type;

    static void from_base(base_type const & in, indicator ind,
        boost::tuple<T0, T1> & out)
    {
        in
            >> boost::tuples::get<0>(out)
            >> boost::tuples::get<1>(out);
    }

    static void to_base(boost::tuple<T0, T1> & in,
        base_type & out, indicator & ind)
    {
        out
            << boost::tuples::get<0>(in)
            << boost::tuples::get<1>(in);
    }
};

template <typename T0, typename T1, typename T2>
struct type_conversion<boost::tuple<T0, T1, T2> >
{
    typedef values base_type;

    static void from_base(base_type const & in, indicator ind,
        boost::tuple<T0, T1, T2> & out)
    {
        in
            >> boost::tuples::get<0>(out)
            >> boost::tuples::get<1>(out)
            >> boost::tuples::get<2>(out);
    }

    static void to_base(boost::tuple<T0, T1, T2> & in,
        base_type & out, indicator & ind)
    {
        out
            << boost::tuples::get<0>(in)
            << boost::tuples::get<1>(in)
            << boost::tuples::get<2>(in);
    }
};

template <typename T0, typename T1, typename T2, typename T3>
struct type_conversion<boost::tuple<T0, T1, T2, T3> >
{
    typedef values base_type;

    static void from_base(base_type const & in, indicator ind,
        boost::tuple<T0, T1, T2, T3> & out)
    {
        in
            >> boost::tuples::get<0>(out)
            >> boost::tuples::get<1>(out)
            >> boost::tuples::get<2>(out)
            >> boost::tuples::get<3>(out);
    }

    static void to_base(boost::tuple<T0, T1, T2, T3> & in,
        base_type & out, indicator & ind)
    {
        out
            << boost::tuples::get<0>(in)
            << boost::tuples::get<1>(in)
            << boost::tuples::get<2>(in)
            << boost::tuples::get<3>(in);
    }
};

template <typename T0, typename T1, typename T2, typename T3, typename T4>
struct type_conversion<boost::tuple<T0, T1, T2, T3, T4> >
{
    typedef values base_type;

    static void from_base(base_type const & in, indicator ind,
        boost::tuple<T0, T1, T2, T3, T4> & out)
    {
        in
            >> boost::tuples::get<0>(out)
            >> boost::tuples::get<1>(out)
            >> boost::tuples::get<2>(out)
            >> boost::tuples::get<3>(out)
            >> boost::tuples::get<4>(out);
    }

    static void to_base(boost::tuple<T0, T1, T2, T3, T4> & in,
        base_type & out, indicator & ind)
    {
        out
            << boost::tuples::get<0>(in)
            << boost::tuples::get<1>(in)
            << boost::tuples::get<2>(in)
            << boost::tuples::get<3>(in)
            << boost::tuples::get<4>(in);
    }
};

template <typename T0, typename T1, typename T2, typename T3, typename T4,
          typename T5>
struct type_conversion<boost::tuple<T0, T1, T2, T3, T4, T5> >
{
    typedef values base_type;

    static void from_base(base_type const & in, indicator ind,
        boost::tuple<T0, T1, T2, T3, T4, T5> & out)
    {
        in
            >> boost::tuples::get<0>(out)
            >> boost::tuples::get<1>(out)
            >> boost::tuples::get<2>(out)
            >> boost::tuples::get<3>(out)
            >> boost::tuples::get<4>(out)
            >> boost::tuples::get<5>(out);
    }

    static void to_base(boost::tuple<T0, T1, T2, T3, T4, T5> & in,
        base_type & out, indicator & ind)
    {
        out
            << boost::tuples::get<0>(in)
            << boost::tuples::get<1>(in)
            << boost::tuples::get<2>(in)
            << boost::tuples::get<3>(in)
            << boost::tuples::get<4>(in)
            << boost::tuples::get<5>(in);
    }
};

template <typename T0, typename T1, typename T2, typename T3, typename T4,
          typename T5, typename T6>
struct type_conversion<boost::tuple<T0, T1, T2, T3, T4, T5, T6> >
{
    typedef values base_type;

    static void from_base(base_type const & in, indicator ind,
        boost::tuple<T0, T1, T2, T3, T4, T5, T6> & out)
    {
        in
            >> boost::tuples::get<0>(out)
            >> boost::tuples::get<1>(out)
            >> boost::tuples::get<2>(out)
            >> boost::tuples::get<3>(out)
            >> boost::tuples::get<4>(out)
            >> boost::tuples::get<5>(out)
            >> boost::tuples::get<6>(out);
    }

    static void to_base(boost::tuple<T0, T1, T2, T3, T4, T5, T6> & in,
        base_type & out, indicator & ind)
    {
        out
            << boost::tuples::get<0>(in)
            << boost::tuples::get<1>(in)
            << boost::tuples::get<2>(in)
            << boost::tuples::get<3>(in)
            << boost::tuples::get<4>(in)
            << boost::tuples::get<5>(in)
            << boost::tuples::get<6>(in);
    }
};

template <typename T0, typename T1, typename T2, typename T3, typename T4,
          typename T5, typename T6, typename T7>
struct type_conversion<boost::tuple<T0, T1, T2, T3, T4, T5, T6, T7> >
{
    typedef values base_type;

    static void from_base(base_type const & in, indicator ind,
        boost::tuple<T0, T1, T2, T3, T4, T5, T6, T7> & out)
    {
        in
            >> boost::tuples::get<0>(out)
            >> boost::tuples::get<1>(out)
            >> boost::tuples::get<2>(out)
            >> boost::tuples::get<3>(out)
            >> boost::tuples::get<4>(out)
            >> boost::tuples::get<5>(out)
            >> boost::tuples::get<6>(out)
            >> boost::tuples::get<7>(out);
    }

    static void to_base(boost::tuple<T0, T1, T2, T3, T4, T5, T6, T7> & in,
        base_type & out, indicator & ind)
    {
        out
            << boost::tuples::get<0>(in)
            << boost::tuples::get<1>(in)
            << boost::tuples::get<2>(in)
            << boost::tuples::get<3>(in)
            << boost::tuples::get<4>(in)
            << boost::tuples::get<5>(in)
            << boost::tuples::get<6>(in)
            << boost::tuples::get<7>(in);
    }
};

template <typename T0, typename T1, typename T2, typename T3, typename T4,
          typename T5, typename T6, typename T7, typename T8>
struct type_conversion<boost::tuple<T0, T1, T2, T3, T4, T5, T6, T7, T8> >
{
    typedef values base_type;

    static void from_base(base_type const & in, indicator ind,
        boost::tuple<T0, T1, T2, T3, T4, T5, T6, T7, T8> & out)
    {
        in
            >> boost::tuples::get<0>(out)
            >> boost::tuples::get<1>(out)
            >> boost::tuples::get<2>(out)
            >> boost::tuples::get<3>(out)
            >> boost::tuples::get<4>(out)
            >> boost::tuples::get<5>(out)
            >> boost::tuples::get<6>(out)
            >> boost::tuples::get<7>(out)
            >> boost::tuples::get<8>(out);
    }

    static void to_base(boost::tuple<T0, T1, T2, T3, T4, T5, T6, T7, T8> & in,
        base_type & out, indicator & ind)
    {
        out
            << boost::tuples::get<0>(in)
            << boost::tuples::get<1>(in)
            << boost::tuples::get<2>(in)
            << boost::tuples::get<3>(in)
            << boost::tuples::get<4>(in)
            << boost::tuples::get<5>(in)
            << boost::tuples::get<6>(in)
            << boost::tuples::get<7>(in)
            << boost::tuples::get<8>(in);
    }
};

template <typename T0, typename T1, typename T2, typename T3, typename T4,
          typename T5, typename T6, typename T7, typename T8, typename T9>
struct type_conversion<boost::tuple<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9> >
{
    typedef values base_type;

    static void from_base(base_type const & in, indicator ind,
        boost::tuple<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9> & out)
    {
        in
            >> boost::tuples::get<0>(out)
            >> boost::tuples::get<1>(out)
            >> boost::tuples::get<2>(out)
            >> boost::tuples::get<3>(out)
            >> boost::tuples::get<4>(out)
            >> boost::tuples::get<5>(out)
            >> boost::tuples::get<6>(out)
            >> boost::tuples::get<7>(out)
            >> boost::tuples::get<8>(out)
            >> boost::tuples::get<9>(out);
    }

    static void to_base(
        boost::tuple<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9> & in,
        base_type & out, indicator & ind)
    {
        out
            << boost::tuples::get<0>(in)
            << boost::tuples::get<1>(in)
            << boost::tuples::get<2>(in)
            << boost::tuples::get<3>(in)
            << boost::tuples::get<4>(in)
            << boost::tuples::get<5>(in)
            << boost::tuples::get<6>(in)
            << boost::tuples::get<7>(in)
            << boost::tuples::get<8>(in)
            << boost::tuples::get<9>(in);
    }
};

} // namespace soci

#else // BOOST_VERSION >= 103500
#   include "boost-fusion.h"
#   include <boost/fusion/adapted/boost_tuple.hpp>
#endif

#endif // SOCI_BOOST_TUPLE_H_INCLUDED
