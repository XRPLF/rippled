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

#ifndef RIPPLE_TYPES_RIPPLEASSETS_H_INCLUDED
#define RIPPLE_TYPES_RIPPLEASSETS_H_INCLUDED

#include <cassert>
#include <functional>
#include <type_traits>

namespace ripple {

/** Identifies a currency in the payment network.
    Currencies are associated with issuers.
    @see RippleIssuer
*/
typedef uint160 RippleCurrency;

/** Identifies the account of a currency issuer.
    Currency IOUs are issued by account holders.
    @see RippleCurrency
*/
typedef uint160 RippleIssuer;

//------------------------------------------------------------------------------

/** Ripple asset specifier, expressed as a currency issuer pair.
    When ByValue is `false`, this only stores references, and the caller
    is responsible for managing object lifetime.
    @see RippleCurrency, RippleIssuer, RippleAssset, RippleAssetRef
*/
template <bool ByValue>
class RippleAssetType
{
public:
    typedef typename std::conditional <ByValue,
        RippleCurrency, RippleCurrency const&>::type Currency;

    typedef typename std::conditional <ByValue,
        RippleIssuer, RippleIssuer const&>::type Issuer;

    Currency currency;
    Issuer issuer;

    RippleAssetType ()
    {
    }

    RippleAssetType (RippleCurrency const& currency_,
        RippleIssuer const& issuer_)
        : currency (currency_)
        , issuer (issuer_)
    {
        // Either XRP and (currency == zero && issuer == zero) or some custom
        // currency and (currency != 0 && issuer != 0)
        assert (currency.isZero () == issuer.isZero ());
    }

    template <bool OtherByValue>
    RippleAssetType (RippleAssetType <OtherByValue> const& other)
        : currency (other.currency)
        , issuer (other.issuer)
    {
    }

    /** Assignment. */
    template <bool MaybeByValue = ByValue, bool OtherByValue>
    std::enable_if_t <MaybeByValue, RippleAssetType&>
    operator= (RippleAssetType <OtherByValue> const& other)
    {
        currency = other.currency;
        issuer = other.issuer;
        return *this;
    }

    bool is_xrp () const
    {
        assert (currency.isZero () == issuer.isZero ());
        if (currency.isZero ())
            return true;
        return false;
    }

    template <class Hasher>
    friend
    void
    hash_append (Hasher& h, RippleAssetType const& r)
    {
        using beast::hash_append;
        hash_append (h, r.currency, r.issuer);
    }
};

/** Ordered comparison.
    The assets are ordered first by currency and then by issuer,
    if the currency is not XRP.
*/
template <bool LhsByValue, bool RhsByValue>
int compare (RippleAssetType <LhsByValue> const& lhs,
    RippleAssetType <RhsByValue> const& rhs)
{
    int const diff (compare (lhs.currency, rhs.currency));
    if (diff != 0)
        return diff;
    if (lhs.is_xrp ())
        return 0;
    return compare (lhs.issuer, rhs.issuer);
}

/** Equality comparison. */
/** @{ */
template <bool LhsByValue, bool RhsByValue>
bool operator== (RippleAssetType <LhsByValue> const& lhs,
    RippleAssetType <RhsByValue> const& rhs)
{
    return compare (lhs, rhs) == 0;
}

template <bool LhsByValue, bool RhsByValue>
bool operator!= (RippleAssetType <LhsByValue> const& lhs,
    RippleAssetType <RhsByValue> const& rhs)
{
    return ! (lhs == rhs);
}
/** @} */

/** Strict weak ordering. */
/** @{ */
template <bool LhsByValue, bool RhsByValue>
bool operator< (RippleAssetType <LhsByValue> const& lhs,
    RippleAssetType <RhsByValue> const& rhs)
{
    return compare (lhs, rhs) < 0;
}

template <bool LhsByValue, bool RhsByValue>
bool operator> (RippleAssetType <LhsByValue> const& lhs,
    RippleAssetType <RhsByValue> const& rhs)
{
    return rhs < lhs;
}

template <bool LhsByValue, bool RhsByValue>
bool operator>= (RippleAssetType <LhsByValue> const& lhs,
    RippleAssetType <RhsByValue> const& rhs)
{
    return ! (lhs < rhs);
}

template <bool LhsByValue, bool RhsByValue>
bool operator<= (RippleAssetType <LhsByValue> const& lhs,
    RippleAssetType <RhsByValue> const& rhs)
{
    return ! (rhs < lhs);
}
/** @} */

//------------------------------------------------------------------------------

typedef RippleAssetType <true> RippleAsset;
typedef RippleAssetType <false> RippleAssetRef;

/** Create an asset specifier by parsing the given JSON.
    Errors, if any, will be returned or injected into the specified result
    JSON object using the JSON-RPC error specification interface.
    @param currency_field The JSON field name of the currency specifier
    @param issuer_field The JSON field name of the issuer specifier
    @param result The JSON in which to store any errors.
    #return An asset representing the parsed JSON if no errors occurred.
*/
RippleAsset make_asset (Json::Value json,
    std::string const& currency_field, std::string const& issuer_field,
        Json::Value* result);

//------------------------------------------------------------------------------

/** Returns an asset specifier that represents XRP. */
inline RippleAssetRef xrp_asset ()
{
    static RippleAsset asset (RippleCurrency (0), RippleIssuer (0));
    return asset;
}

//------------------------------------------------------------------------------

/** Specifies an order book.
    The order book is defined by the input asset ('in') and output
    asset ('out').
*/
template <bool ByValue>
class RippleBookType
{
public:
    typedef RippleAssetType <ByValue> AssetType;

    AssetType in;
    AssetType out;

    RippleBookType ()
    {
    }

    RippleBookType (AssetType const& in_, AssetType const& out_)
        : in (in_)
        , out (out_)
    {
    }

    template <bool OtherByValue>
    RippleBookType (RippleBookType <OtherByValue> const& other)
        : in (other.in)
        , out (other.out)
    {
    }

    /** Assignment.
        This is only valid when ByValue == `true`
    */
    template <bool OtherByValue>
    RippleBookType& operator= (RippleBookType <OtherByValue> const& other)
    {
        in = other.in;
        out = other.out;
        return *this;
    }

    template <class Hasher>
    friend
    void
    hash_append (Hasher& h, RippleBookType const& b)
    {
        using beast::hash_append;
        hash_append (h, b.in, b.out);
    }
};

/** Ordered comparison. */
template <bool LhsByValue, bool RhsByValue>
int compare (RippleBookType <LhsByValue> const& lhs,
    RippleBookType <RhsByValue> const& rhs)
{
    int const diff (compare (lhs.in, rhs.in));
    if (diff != 0)
        return diff;
    return compare (lhs.out, rhs.out);
}

/** Equality comparison. */
/** @{ */
template <bool LhsByValue, bool RhsByValue>
bool operator== (RippleBookType <LhsByValue> const& lhs,
    RippleBookType <RhsByValue> const& rhs)
{
    return (lhs.in == rhs.in) &&
           (lhs.out == rhs.out);
}

template <bool LhsByValue, bool RhsByValue>
bool operator!= (RippleBookType <LhsByValue> const& lhs,
    RippleBookType <RhsByValue> const& rhs)
{
    return (lhs.in != rhs.in) ||
           (lhs.out != rhs.out);
}
/** @} */

/** Strict weak ordering. */
/** @{ */
template <bool LhsByValue, bool RhsByValue>
bool operator< (RippleBookType <LhsByValue> const& lhs,
    RippleBookType <RhsByValue> const& rhs)
{
    int const diff (compare (lhs.in, rhs.in));
    if (diff != 0)
        return diff < 0;
    return lhs.out < rhs.out;
}

template <bool LhsByValue, bool RhsByValue>
bool operator> (RippleBookType <LhsByValue> const& lhs,
    RippleBookType <RhsByValue> const& rhs)
{
    return rhs < lhs;
}

template <bool LhsByValue, bool RhsByValue>
bool operator>= (RippleBookType <LhsByValue> const& lhs,
    RippleBookType <RhsByValue> const& rhs)
{
    return ! (lhs < rhs);
}

template <bool LhsByValue, bool RhsByValue>
bool operator<= (RippleBookType <LhsByValue> const& lhs,
    RippleBookType <RhsByValue> const& rhs)
{
    return ! (rhs < lhs);
}
/** @} */

//------------------------------------------------------------------------------

typedef RippleBookType <true> RippleBook;
typedef RippleBookType <false> RippleBookRef;

}

//------------------------------------------------------------------------------

namespace std {

template <bool ByValue>
struct hash <ripple::RippleAssetType <ByValue>>
    : private boost::base_from_member <std::hash <ripple::RippleCurrency>, 0>
    , private boost::base_from_member <std::hash <ripple::RippleIssuer>, 1>
{
private:
    typedef boost::base_from_member <
        std::hash <ripple::RippleCurrency>, 0> currency_hash_type;
    typedef boost::base_from_member <
        std::hash <ripple::RippleIssuer>, 1> issuer_hash_type;

public:
    typedef std::size_t value_type;
    typedef ripple::RippleAssetType <ByValue> argument_type;

    value_type operator() (argument_type const& value) const
    {
        value_type result (currency_hash_type::member (value.currency));
        if (! value.is_xrp ())
            boost::hash_combine (result,
                issuer_hash_type::member (value.issuer));
        return result;
    }
};

//------------------------------------------------------------------------------

template <bool ByValue>
struct hash <ripple::RippleBookType <ByValue>>
{
private:
    typedef std::hash <ripple::RippleAssetType <ByValue>> hasher;

    hasher m_hasher;

public:
    typedef std::size_t value_type;
    typedef ripple::RippleBookType <ByValue> argument_type;

    value_type operator() (argument_type const& value) const
    {
        value_type result (m_hasher (value.in));
        boost::hash_combine (result, m_hasher (value.out));
        return result;
    }
};

}

//------------------------------------------------------------------------------

namespace boost {

template <bool ByValue>
struct hash <ripple::RippleAssetType <ByValue>>
    : std::hash <ripple::RippleAssetType <ByValue>>
{
    typedef std::hash <ripple::RippleAssetType <ByValue>> Base;
    // VFALCO NOTE broken in vs2012
    //using Base::Base; // inherit ctors
};

template <bool ByValue>
struct hash <ripple::RippleBookType <ByValue>>
    : std::hash <ripple::RippleBookType <ByValue>>
{
    typedef std::hash <ripple::RippleBookType <ByValue>> Base;
    // VFALCO NOTE broken in vs2012
    //using Base::Base; // inherit ctors
};

}

#endif
