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

#ifndef RIPPLE_PROTOCOL_STLEDGERENTRY_H_INCLUDED
#define RIPPLE_PROTOCOL_STLEDGERENTRY_H_INCLUDED

#include <ripple/protocol/Keylet.h>
#include <ripple/protocol/STObject.h>

namespace ripple {

class Invariants_test;

class STLedgerEntry final : public STObject, public CountedObject<STLedgerEntry>
{
    uint256 const key_;
    LedgerEntryType const type_;

public:
    /** Create an empty object with the given key and type. */
    explicit STLedgerEntry(Keylet const& k);
    STLedgerEntry(LedgerEntryType type, uint256 const& key);
    STLedgerEntry(Slice data, uint256 const& key);

    /** Special constructor used by the unit testing framework. */
    STLedgerEntry(Invariants_test const&, Keylet const& k);

    SerializedTypeID
    getSType() const override;

    std::string
    getFullText() const override;

    std::string
    getText() const override;

    Json::Value
    getJson(JsonOptions options) const override;

    /** Returns the 'key' (or 'index') of this item.
        The key identifies this entry's position in
        the SHAMap associative container.
    */
    uint256 const&
    key() const;

    LedgerEntryType
    getType() const;

    // is this a ledger entry that can be threaded
    bool
    isThreadedType() const;

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

using SLE = STLedgerEntry;

inline STLedgerEntry::STLedgerEntry(LedgerEntryType type, uint256 const& key)
    : STLedgerEntry(Keylet(type, key))
{
}

inline STLedgerEntry::STLedgerEntry(Invariants_test const&, Keylet const& k)
    : STObject(sfLedgerEntry), key_(k.key), type_(k.type)
{
}

/** Returns the 'key' that determines the position of this item in a SHAMap. */
inline uint256 const&
STLedgerEntry::key() const
{
    return key_;
}

inline LedgerEntryType
STLedgerEntry::getType() const
{
    return type_;
}

}  // namespace ripple

#endif
