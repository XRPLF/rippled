
//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#ifndef RIPPLE_SERVER_LOWESTLAYER_H_INCLUDED
#define RIPPLE_SERVER_LOWESTLAYER_H_INCLUDED

#if BOOST_VERSION >= 107000
#include <boost/beast/core/stream_traits.hpp>
#else
#include <boost/beast/core/type_traits.hpp>
#endif

namespace ripple {

// Before boost 1.70, get_lowest_layer required an explicit templat parameter
template <class T>
decltype(auto)
get_lowest_layer(T& t) noexcept
{
#if BOOST_VERSION >= 107000
    return boost::beast::get_lowest_layer(t);
#else
    return t.lowest_layer();
#endif
}

}  // namespace ripple

#endif
