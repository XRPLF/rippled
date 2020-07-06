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
#include <ripple/protocol/SField.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ostream>
#include <stdexcept>

namespace ripple {

template <class TIn, class TOut>
class TOfferBase
{
protected:
    Issue issIn_;
    Issue issOut_;
};

template <>
class TOfferBase<STAmount, STAmount>
{
public:
    explicit TOfferBase() = default;
};

template <class TIn = STAmount, class TOut = STAmount>
class TOffer : private TOfferBase<TIn, TOut>
{
private:
    std::shared_ptr<SLE> m_entry;
    Quality m_quality;
    AccountID m_account;

    TAmounts<TIn, TOut> m_amounts;
    void
    setFieldAmounts();

public:
    TOffer() = default;

    TOffer(std::shared_ptr<SLE> const& entry, Quality quality);

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
    AccountID const&
    owner() const
    {
        return m_account;
    }

    /** Returns the in and out amounts.
        Some or all of the out amount may be unfunded.
    */
    TAmounts<TIn, TOut> const&
    amount() const
    {
        return m_amounts;
    }

    /** Returns `true` if no more funds can flow through this offer. */
    bool
    fully_consumed() const
    {
        if (m_amounts.in <= beast::zero)
            return true;
        if (m_amounts.out <= beast::zero)
            return true;
        return false;
    }

    /** Adjusts the offer to indicate that we consumed some (or all) of it. */
    void
    consume(ApplyView& view, TAmounts<TIn, TOut> const& consumed)
    {
        if (consumed.in > m_amounts.in)
            Throw<std::logic_error>("can't consume more than is available.");

        if (consumed.out > m_amounts.out)
            Throw<std::logic_error>("can't produce more than is available.");

        m_amounts -= consumed;
        setFieldAmounts();
        view.update(m_entry);
    }

    std::string
    id() const
    {
        return to_string(m_entry->key());
    }

    uint256
    key() const
    {
        return m_entry->key();
    }

    Issue
    issueIn() const;
    Issue
    issueOut() const;
};

using Offer = TOffer<>;

template <class TIn, class TOut>
TOffer<TIn, TOut>::TOffer(std::shared_ptr<SLE> const& entry, Quality quality)
    : m_entry(entry)
    , m_quality(quality)
    , m_account(m_entry->getAccountID(sfAccount))
{
    auto const tp = m_entry->getFieldAmount(sfTakerPays);
    auto const tg = m_entry->getFieldAmount(sfTakerGets);
    m_amounts.in = toAmount<TIn>(tp);
    m_amounts.out = toAmount<TOut>(tg);
    this->issIn_ = tp.issue();
    this->issOut_ = tg.issue();
}

template <>
inline TOffer<STAmount, STAmount>::TOffer(
    std::shared_ptr<SLE> const& entry,
    Quality quality)
    : m_entry(entry)
    , m_quality(quality)
    , m_account(m_entry->getAccountID(sfAccount))
    , m_amounts(
          m_entry->getFieldAmount(sfTakerPays),
          m_entry->getFieldAmount(sfTakerGets))
{
}

template <class TIn, class TOut>
void
TOffer<TIn, TOut>::setFieldAmounts()
{
#ifdef _MSC_VER
    assert(0);
#else
    static_assert(sizeof(TOut) == -1, "Must be specialized");
#endif
}

template <>
inline void
TOffer<STAmount, STAmount>::setFieldAmounts()
{
    m_entry->setFieldAmount(sfTakerPays, m_amounts.in);
    m_entry->setFieldAmount(sfTakerGets, m_amounts.out);
}

template <>
inline void
TOffer<IOUAmount, IOUAmount>::setFieldAmounts()
{
    m_entry->setFieldAmount(sfTakerPays, toSTAmount(m_amounts.in, issIn_));
    m_entry->setFieldAmount(sfTakerGets, toSTAmount(m_amounts.out, issOut_));
}

template <>
inline void
TOffer<IOUAmount, XRPAmount>::setFieldAmounts()
{
    m_entry->setFieldAmount(sfTakerPays, toSTAmount(m_amounts.in, issIn_));
    m_entry->setFieldAmount(sfTakerGets, toSTAmount(m_amounts.out));
}

template <>
inline void
TOffer<XRPAmount, IOUAmount>::setFieldAmounts()
{
    m_entry->setFieldAmount(sfTakerPays, toSTAmount(m_amounts.in));
    m_entry->setFieldAmount(sfTakerGets, toSTAmount(m_amounts.out, issOut_));
}

template <class TIn, class TOut>
Issue
TOffer<TIn, TOut>::issueIn() const
{
    return this->issIn_;
}

template <>
inline Issue
TOffer<STAmount, STAmount>::issueIn() const
{
    return m_amounts.in.issue();
}

template <class TIn, class TOut>
Issue
TOffer<TIn, TOut>::issueOut() const
{
    return this->issOut_;
}

template <>
inline Issue
TOffer<STAmount, STAmount>::issueOut() const
{
    return m_amounts.out.issue();
}

template <class TIn, class TOut>
inline std::ostream&
operator<<(std::ostream& os, TOffer<TIn, TOut> const& offer)
{
    return os << offer.id();
}

}  // namespace ripple

#endif
