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

#ifndef RIPPLE_APP_PATHS_IMPL_FLAT_SETS_H_INCLUDED
#define RIPPLE_APP_PATHS_IMPL_FLAT_SETS_H_INCLUDED

#include <boost/container/flat_set.hpp>

namespace ripple {

/** Given two flat sets dst and src, compute dst = dst union src

    @param dst set to store the resulting union, and also a source of elements
   for the union
    @param src second source of elements for the union
 */
template <class T>
void
SetUnion(
    boost::container::flat_set<T>& dst,
    boost::container::flat_set<T> const& src)
{
    if (src.empty())
        return;

    dst.reserve(dst.size() + src.size());
    dst.insert(
        boost::container::ordered_unique_range_t{}, src.begin(), src.end());
}

}  // namespace ripple

#endif
