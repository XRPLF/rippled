//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_BOOST_FUSION_H_INCLUDED
#define SOCI_BOOST_FUSION_H_INCLUDED

#ifndef SOCI_MAX_FUSION_SEQUENCE_LENGTH
#define SOCI_MAX_FUSION_SEQUENCE_LENGTH 10
#endif

#include "values.h"
#include "type-conversion-traits.h"
// boost
#include <boost/fusion/container/vector.hpp>
#include <boost/fusion/sequence/intrinsic/at.hpp>
#include <boost/fusion/sequence/intrinsic/size.hpp>
#include <boost/fusion/include/at.hpp>
#include <boost/fusion/support/is_sequence.hpp>
#include <boost/preprocessor/repetition/repeat.hpp>
#include <boost/preprocessor/repetition/repeat_from_to.hpp>
#include <boost/utility/enable_if.hpp>

namespace soci
{
namespace detail
{

template <typename Seq, int size>
struct type_conversion;

#define SOCI_READ_FROM_BASE(z, k, data) \
    >> boost::fusion::at_c<k>(out)
/**/

#define SOCI_READ_TO_BASE(z, k, data) \
    << boost::fusion::at_c<k>(in)
/**/

#define SOCI_TYPE_CONVERSION_FUSION(z, k, data) \
    template <typename Seq> \
    struct type_conversion<Seq, k> \
    { \
        typedef values base_type; \
     \
        static void from_base(base_type const & in, indicator /*ind*/, Seq & out) \
        { \
            in \
                BOOST_PP_REPEAT(k, SOCI_READ_FROM_BASE, BOOST_PP_EMPTY) \
            ; \
        } \
     \
        static void to_base(Seq & in, base_type & out, indicator & /*ind*/) \
        { \
            out \
                BOOST_PP_REPEAT(k, SOCI_READ_TO_BASE, BOOST_PP_EMPTY) \
            ; \
        } \
    };
/**/

BOOST_PP_REPEAT_FROM_TO(1, BOOST_PP_ADD(SOCI_MAX_FUSION_SEQUENCE_LENGTH, 1), SOCI_TYPE_CONVERSION_FUSION, BOOST_PP_EMPTY)

#undef SOCI_TYPE_CONVERSION_FUSION
#undef SOCI_READ_FROM_BASE
#undef SOCI_READ_TO_BASE

} // namespace detail

template <typename T>
struct type_conversion<T, 
    typename boost::enable_if<
        boost::fusion::traits::is_sequence<T>
    >::type >
{
    typedef values base_type;

private:
    typedef typename boost::fusion::result_of::size<T>::type size;
    typedef detail::type_conversion<T, size::value> converter;

public:
    static void from_base(base_type const & in, indicator ind, T& out)
    {
        converter::from_base( in, ind, out );
    }

    static void to_base(T& in, base_type & out, indicator & ind)
    {
        converter::to_base( in, out, ind );
    }
};

} // namespace soci

#endif // SOCI_BOOST_FUSION_H_INCLUDED
