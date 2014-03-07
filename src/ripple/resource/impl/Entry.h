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

namespace ripple {
namespace Resource {

typedef beast::abstract_clock <std::chrono::seconds> clock_type;

// An entry in the table
struct Entry : public beast::List <Entry>::Node
{
    // Dummy argument is necessary for zero-copy construction of elements
    Entry (int)
        : refcount (0)
        , remote_balance (0)
        , disposition (ok)
        , lastWarningTime (0)
        , whenExpires (0)
    {
    }

    std::string to_string() const
    {
        switch (key->kind)
        {
        case kindInbound:   return key->address.to_string();
        case kindOutbound:  return key->address.to_string();
        case kindAdmin:     return std::string ("\"") + key->name + "\"";
        default:
            bassertfalse;
        }

        return "(undefined)";
    }

    // Returns `true` if this connection is privileged
    bool admin () const
    {
        return key->kind == kindAdmin;
    }

    // Balance including remote contributions
    int balance (clock_type::rep const now)
    {
        return local_balance.value (now) + remote_balance;
    }

    // Add a charge and return normalized balance
    // including contributions from imports.
    int add (int charge, clock_type::rep const now)
    {
        return local_balance.add (charge, now) + remote_balance;
    }

    // Back pointer to the map key (bit of a hack here)
    Key const* key;

    // Number of Consumer references
    int refcount;

    // Exponentially decaying balance of resource consumption
    DecayingSample <decayWindowSeconds> local_balance;

    // Normalized balance contribution from imports
    int remote_balance;

    // Disposition
    Disposition disposition;

    // Time of the last warning
    clock_type::rep lastWarningTime;

    // For inactive entries, time after which this entry will be erased
    clock_type::rep whenExpires;
};

std::ostream& operator<< (std::ostream& os, Entry const& v)
{
    os << v.to_string();
    return os;
}

}
}

#endif
