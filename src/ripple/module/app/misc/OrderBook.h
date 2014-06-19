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

#ifndef RIPPLE_ORDERBOOK_H
#define RIPPLE_ORDERBOOK_H

namespace ripple {

/** Describes a serialized ledger entry for an order book. */
class OrderBook : beast::LeakChecked <OrderBook>
{
public:
    typedef std::shared_ptr <OrderBook> pointer;
    typedef std::shared_ptr <OrderBook> const& ref;

public:
    /** Construct from a currency specification.

        @param index ???
        @param currencyIn  The base currency.
        @param currencyOut The destination currency.
        @param issuerIn    The base issuer.
        @param issuerOut   The destination issuer.
    */
    // VFALCO NOTE what is the meaning of the index parameter?
    // VFALCO TODO Replace with RippleAsset
    OrderBook (uint256 const& index,
               Currency const& currencyIn,
               Currency const& currencyOut,
               Account const& issuerIn,
               Account const& issuerOut);

    uint256 const& getBookBase () const
    {
        return mBookBase;
    }

    Currency const& getCurrencyIn () const
    {
        return mCurrencyIn;
    }

    Currency const& getCurrencyOut () const
    {
        return mCurrencyOut;
    }

    Account const& getIssuerIn () const
    {
        return mIssuerIn;
    }

    Account const& getIssuerOut () const
    {
        return mIssuerOut;
    }

private:
    uint256 const mBookBase;
    Currency const mCurrencyIn;
    Currency const mCurrencyOut;
    Account const mIssuerIn;
    Account const mIssuerOut;
};

} // ripple

#endif
