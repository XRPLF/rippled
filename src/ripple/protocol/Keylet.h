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

#ifndef RIPPLE_PROTOCOL_KEYLET_H_INCLUDED
#define RIPPLE_PROTOCOL_KEYLET_H_INCLUDED

#include <ripple/basics/base_uint.h>
#include <ripple/protocol/LedgerFormats.h>

namespace ripple {

class STLedgerEntry;

/** A pair of SHAMap key and LedgerEntryType.

    A Keylet identifies both a key in the state map
    and its ledger entry type.

    @note Keylet is a portmanteau of the words key
          and LET, an acronym for LedgerEntryType.
*/
struct Keylet
{
    uint256 key;
    LedgerEntryType type;

    Keylet(LedgerEntryType type_, uint256 const& key_) : key(key_), type(type_)
    {
    }

    /** Returns true if the SLE matches the type */
    bool
    check(STLedgerEntry const&) const;
};

}  // namespace ripple

#endif
