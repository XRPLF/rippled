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

#ifndef RIPPLE_RESOURCE_GOSSIP_H_INCLUDED
#define RIPPLE_RESOURCE_GOSSIP_H_INCLUDED

#include <xrpl/beast/net/IPEndpoint.h>

#include <vector>

namespace ripple {
namespace Resource {

/** Data format for exchanging consumption information across peers. */
struct Gossip
{
    explicit Gossip() = default;

    /** Describes a single consumer. */
    struct Item
    {
        explicit Item() = default;

        int balance;
        beast::IP::Endpoint address;
    };

    std::vector<Item> items;
};

}  // namespace Resource
}  // namespace ripple

#endif
