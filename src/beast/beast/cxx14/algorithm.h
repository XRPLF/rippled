//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_CXX14_ALGORITHM_H_INCLUDED
#define BEAST_CXX14_ALGORITHM_H_INCLUDED

#include <beast/cxx14/config.h>
#include <beast/cxx14/functional.h>

#include <algorithm>

#if ! BEAST_NO_CXX14_EQUAL

namespace std {

namespace detail {

template <class Pred, class FwdIt1, class FwdIt2>
bool equal (FwdIt1 first1, FwdIt1 last1,
    FwdIt2 first2, FwdIt2 last2, Pred pred,
        std::input_iterator_tag, std::input_iterator_tag)
{
    for (; first1 != last1 && first2 != last2; ++first1, ++first2)
        if (! pred (*first1, *first2))
            return false;
    return first1 == last1 && first2 == last2;
}

template <class Pred, class RanIt1, class RanIt2>
bool equal (RanIt1 first1, RanIt1 last1,
    RanIt2 first2, RanIt2 last2, Pred pred,
        random_access_iterator_tag,
            random_access_iterator_tag )
{
    if (std::distance (first1, last1) !=
            std::distance (first2, last2))
        return false;
    for (; first1 != last1; ++first1, ++first2)
        if (! pred (*first1, *first2))
            return false;
    return true;
}

}

/** C++14 implementation of std::equal. */
/** @{ */
template <class FwdIt1, class FwdIt2>
bool equal (FwdIt1 first1, FwdIt1 last1,
    FwdIt2 first2, FwdIt2 last2)
{
    return std::detail::equal (first1, last1,
        first2, last2, std::equal_to <void>(),
            typename std::iterator_traits <
                FwdIt1>::iterator_category(),
                    typename std::iterator_traits <
                        FwdIt2>::iterator_category());
}

template <class FwdIt1, class FwdIt2, class Pred>
bool equal (FwdIt1 first1, FwdIt1 last1,
    FwdIt2 first2, FwdIt2 last2, Pred pred)
{
    return std::detail::equal <
        typename std::add_lvalue_reference <Pred>::type> (
            first1, last1, first2, last2, pred,
                typename std::iterator_traits <
                    FwdIt1>::iterator_category(),
                        typename std::iterator_traits <
                            FwdIt2>::iterator_category());
}
/** @} */

}

#endif

#endif
