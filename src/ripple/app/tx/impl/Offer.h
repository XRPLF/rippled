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

#ifndef RIPPLE_APP_BOOK_OFFER_H_INCLUDED
#define RIPPLE_APP_BOOK_OFFER_H_INCLUDED

#include <ripple/basics/contract.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/protocol/SField.h>
#include <ostream>
#include <stdexcept>

namespace ripple {

class Offer
{
private:
    SLE::pointer m_entry;
    Quality m_quality;
    AccountID m_account;

    mutable Amounts m_amounts;

public:
    Offer() = default;

    Offer (SLE::pointer const& entry, Quality quality)
        : m_entry (entry)
        , m_quality (quality)
        , m_account (m_entry->getAccountID (sfAccount))
        , m_amounts (
            m_entry->getFieldAmount (sfTakerPays),
            m_entry->getFieldAmount (sfTakerGets))
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
    quality () const noexcept
    {
        return m_quality;
    }

    /** Returns the account id of the offer's owner. */
    AccountID const&
    owner () const
    {
        return m_account;
    }

    /** Returns the in and out amounts.
        Some or all of the out amount may be unfunded.
    */
    Amounts const&
    amount () const
    {
        return m_amounts;
    }

    /** Returns `true` if no more funds can flow through this offer. */
    bool
    fully_consumed () const
    {
        if (m_amounts.in <= zero)
            return true;
        if (m_amounts.out <= zero)
            return true;
        return false;
    }

    /** Adjusts the offer to indicate that we consumed some (or all) of it. */
    void
    consume (ApplyView& view,
        Amounts const& consumed) const
    {
        if (consumed.in > m_amounts.in)
            Throw<std::logic_error> ("can't consume more than is available.");

        if (consumed.out > m_amounts.out)
            Throw<std::logic_error> ("can't produce more than is available.");

        m_amounts.in -= consumed.in;
        m_amounts.out -= consumed.out;

        m_entry->setFieldAmount (sfTakerPays, m_amounts.in);
        m_entry->setFieldAmount (sfTakerGets, m_amounts.out);

        view.update (m_entry);
    }

    std::string id () const
    {
        return to_string (m_entry->getIndex());
    }
};

inline
std::ostream&
operator<< (std::ostream& os, Offer const& offer)
{
    return os << offer.id ();
}

}

#endif
