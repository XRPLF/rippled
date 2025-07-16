//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_STRHEX_H_INCLUDED
#define RIPPLE_BASICS_STRHEX_H_INCLUDED

#include <boost/algorithm/hex.hpp>
#include <boost/endian/conversion.hpp>

namespace ripple {

template <class FwdIt>
std::string
strHex(FwdIt begin, FwdIt end)
{
    static_assert(
        std::is_convertible<
            typename std::iterator_traits<FwdIt>::iterator_category,
            std::forward_iterator_tag>::value,
        "FwdIt must be a forward iterator");
    std::string result;
    result.reserve(2 * std::distance(begin, end));
    boost::algorithm::hex(begin, end, std::back_inserter(result));
    return result;
}

template <class T, class = decltype(std::declval<T>().begin())>
std::string
strHex(T const& from)
{
    return strHex(from.begin(), from.end());
}

}  // namespace ripple

#endif
