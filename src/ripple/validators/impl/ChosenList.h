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

#ifndef RIPPLE_VALIDATORS_CHOSENLIST_H_INCLUDED
#define RIPPLE_VALIDATORS_CHOSENLIST_H_INCLUDED

#include <ripple/common/UnorderedContainers.h>

namespace ripple {
namespace Validators {

class ChosenList : public beast::SharedObject
{
public:
    typedef beast::SharedPtr <ChosenList> Ptr;

    struct Info
    {
        Info ()
        {
        }
    };

    typedef ripple::unordered_map <RipplePublicKey, Info,
                                 beast::hardened_hash<RipplePublicKey>> MapType;

    ChosenList (std::size_t expectedSize = 0)
    {
        // Available only in recent boost versions?
        //m_map.reserve (expectedSize);
    }

    MapType const& map() const
    {
        return m_map;
    }

    std::size_t size () const noexcept
    {
        return m_map.size ();
    }

    void insert (RipplePublicKey const& key, Info const& info) noexcept
    {
        m_map [key] = info;
    }

    bool containsPublicKey (RipplePublicKey const& publicKey) const noexcept
    {
        return m_map.find (publicKey) != m_map.cend ();
    }

    bool containsPublicKeyHash (RipplePublicKeyHash const& publicKeyHash) const noexcept
    {
        return false;
    }

private:
    MapType m_map;
};

}
}

#endif
