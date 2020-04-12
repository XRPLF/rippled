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

#ifndef RIPPLE_SHAMAP_SHAMAPMISSINGNODE_H_INCLUDED
#define RIPPLE_SHAMAP_SHAMAPMISSINGNODE_H_INCLUDED

#include <ripple/basics/base_uint.h>
#include <ripple/shamap/SHAMapTreeNode.h>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace ripple {

enum class SHAMapType
{
    TRANSACTION  = 1,    // A tree of transactions
    STATE        = 2,    // A tree of state nodes
    FREE         = 3,    // A tree not part of a ledger
};

inline
std::string
to_string(SHAMapType t)
{
    switch (t)
    {
    case SHAMapType::TRANSACTION:
        return "Transaction Tree";
    case SHAMapType::STATE:
        return "State Tree";
    case SHAMapType::FREE:
        return "Free Tree";
    default:
        return std::to_string(safe_cast<std::underlying_type_t<SHAMapType>>(t));
    }
}

class SHAMapMissingNode
    : public std::runtime_error
{
public:
    SHAMapMissingNode (SHAMapType t, SHAMapHash const& hash)
        : std::runtime_error("Missing Node: " +
            to_string(t) + ": hash " + to_string(hash))
    {
    }

    SHAMapMissingNode (SHAMapType t, uint256 const& id)
        : std::runtime_error ("Missing Node: " +
            to_string(t) + ": id " + to_string(id))
    {
    }
};

} // ripple

#endif
