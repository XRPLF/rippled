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

#ifndef RIPPLE_RESOURCE_IMPORT_H_INCLUDED
#define RIPPLE_RESOURCE_IMPORT_H_INCLUDED

#include <ripple/resource/Consumer.h>
#include <ripple/resource/impl/Entry.h>

namespace ripple {
namespace Resource {

/** A set of imported consumer data from a gossip origin. */
struct Import
{
    struct Item
    {
        explicit Item() = default;

        int balance;
        Consumer consumer;
    };

    // Dummy argument required for zero-copy construction
    Import(int = 0) : whenExpires()
    {
    }

    // When the imported data expires
    clock_type::time_point whenExpires;

    // List of remote entries
    std::vector<Item> items;
};

}  // namespace Resource
}  // namespace ripple

#endif
