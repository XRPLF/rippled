//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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

#ifndef RIPPLE_CORE_OFFER_H_INCLUDED
#define RIPPLE_CORE_OFFER_H_INCLUDED

#include <ripple/module/app/book/Amounts.h>
#include <ripple/module/app/book/Quality.h>
#include <ripple/module/app/book/Types.h>

#include <ripple/module/app/misc/SerializedLedger.h>
#include <ripple/module/data/protocol/FieldNames.h>

#include <beast/utility/noexcept.h>

#include <ostream>

namespace ripple {
namespace core {

class Offer
{
public:
    typedef Amount amount_type;

private:
    SLE::pointer m_entry;
    Quality m_quality;

public:
    Offer() = default;

    Offer (SLE::pointer const& entry, Quality quality)
        : m_entry (entry)
        , m_quality (quality)
    {
    }

    /** Returns the quality of the offer.
        Conceptually, the quality is the ratio of output to input currency.
        The implementation calculates it as the ratio of input to output
        currency (so it sorts ascending). The quality is computed at the time
        the offer is placed, and never changes for the lifetime of the offer.
        This is an important business rule that maintains accuracy when an
        offer is partially filled; Subsequent partial fills will use the
        original quality.
    */
    Quality const
    quality() const noexcept
    {
        return m_quality;
    }

    /** Returns the account id of the offer's owner. */
    Account const
    account() const
    {
        return m_entry->getFieldAccount160 (sfAccount);
    }

    /** Returns the in and out amounts.
        Some or all of the out amount may be unfunded.
    */
    Amounts const
    amount() const
    {
        return Amounts (m_entry->getFieldAmount (sfTakerPays),
            m_entry->getFieldAmount (sfTakerGets));
    }

    /** Returns `true` if no more funds can flow through this offer. */
    bool
    fully_consumed() const
    {
        if (m_entry->getFieldAmount (sfTakerPays) <= zero)
            return true;
        if (m_entry->getFieldAmount (sfTakerGets) <= zero)
            return true;
        return false;
    }

    /** Returns the ledger entry underlying the offer. */
    // AVOID USING THIS
    SLE::pointer
    entry() const noexcept
    {
        return m_entry;
    }
};

inline
std::ostream&
operator<< (std::ostream& os, Offer const& offer)
{
    return os << offer.entry()->getIndex();
}

}
}

#endif
