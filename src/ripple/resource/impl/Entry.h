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

#ifndef RIPPLE_RESOURCE_ENTRY_H_INCLUDED
#define RIPPLE_RESOURCE_ENTRY_H_INCLUDED

#include <ripple/basics/DecayingSample.h>
#include <ripple/resource/impl/Key.h>
#include <ripple/resource/impl/Tuning.h>
#include <ripple/beast/clock/abstract_clock.h>
#include <ripple/beast/core/List.h>
#include <cassert>

namespace ripple {
namespace Resource {

using clock_type = beast::abstract_clock <std::chrono::steady_clock>;

// An entry in the table
// VFALCO DEPRECATED using boost::intrusive list
struct Entry
    : public beast::List <Entry>::Node
{
    Entry () = delete;

    /**
       @param now Construction time of Entry.
    */
    explicit Entry(clock_type::time_point const now)
        : refcount (0)
        , local_balance (now)
        , remote_balance (0)
        , lastWarningTime ()
        , whenExpires ()
    {
    }

    std::string to_string() const
    {
        switch (key->kind)
        {
        case kindInbound:   return key->address.to_string();
        case kindOutbound:  return key->address.to_string();
        case kindUnlimited: return std::string ("\"") + key->name + "\"";
        default:
            assert(false);
        }

        return "(undefined)";
    }

    /**
     * Returns `true` if this connection should have no
     * resource limits applied--it is still possible for certain RPC commands
     * to be forbidden, but that depends on Role.
     */
    bool isUnlimited () const
    {
        return key->kind == kindUnlimited;
    }

    // Balance including remote contributions
    int balance (clock_type::time_point const now)
    {
        return local_balance.value (now) + remote_balance;
    }

    // Add a charge and return normalized balance
    // including contributions from imports.
    int add (int charge, clock_type::time_point const now)
    {
        return local_balance.add (charge, now) + remote_balance;
    }

    // Back pointer to the map key (bit of a hack here)
    Key const* key;

    // Number of Consumer references
    int refcount;

    // Exponentially decaying balance of resource consumption
    DecayingSample <decayWindowSeconds, clock_type> local_balance;

    // Normalized balance contribution from imports
    int remote_balance;

    // Time of the last warning
    clock_type::time_point lastWarningTime;

    // For inactive entries, time after which this entry will be erased
    clock_type::time_point whenExpires;
};

inline std::ostream& operator<< (std::ostream& os, Entry const& v)
{
    os << v.to_string();
    return os;
}

}
}

#endif
