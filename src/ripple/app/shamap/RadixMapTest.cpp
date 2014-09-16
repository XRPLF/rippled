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

#include <cassert>

namespace ripple {
namespace RadixMap {

std::shared_ptr <Item> make_random_item (beast::Random& r)
{
    Serializer s;
    for (int d = 0; d < 3; ++d)
        s.add32 (r.nextInt ());
    return std::make_shared <Item> (
        to256(s.getRIPEMD160()), s.peekData ());
}

//------------------------------------------------------------------------------

void add_random_items (std::size_t n, Table& t, beast::Random& r)
{
    while (n--)
    {
        std::shared_ptr <SHAMapItem> item (
            make_random_item (r));
        auto const result (t.addItem (*item, false, false));
        assert (result);
    }
}

}
}
