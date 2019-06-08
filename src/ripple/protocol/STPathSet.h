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

#ifndef RIPPLE_PROTOCOL_STPATHSET_H_INCLUDED
#define RIPPLE_PROTOCOL_STPATHSET_H_INCLUDED

#include <ripple/json/json_value.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/STBase.h>
#include <ripple/protocol/UintTypes.h>
#include <boost/optional.hpp>
#include <cassert>
#include <cstddef>

namespace ripple {

class STPathElement
{
public:
    enum Type
    {
        typeNone        = 0x00,
        typeAccount     = 0x01, // Rippling through an account (vs taking an offer).
        typeCurrency    = 0x10, // Currency follows.
        typeIssuer      = 0x20, // Issuer follows.
        typeBoundary    = 0xFF, // Boundary between alternate paths.
        typeAll = typeAccount | typeCurrency | typeIssuer,
                                // Combination of all types.
    };

private:
    static
    std::size_t
    get_hash (STPathElement const& element);

public:
    STPathElement(
        boost::optional<AccountID> const& account,
        boost::optional<Currency> const& currency,
        boost::optional<AccountID> const& issuer)
        : mType (typeNone)
    {
        if (! account)
        {
            is_offer_ = true;
        }
        else
        {
            is_offer_ = false;
            mAccountID = *account;
            mType |= typeAccount;
            assert(mAccountID != noAccount());
        }

        if (currency)
        {
            mCurrencyID = *currency;
            mType |= typeCurrency;
        }

        if (issuer)
        {
            mIssuerID = *issuer;
            mType |= typeIssuer;
            assert(mIssuerID != noAccount());
        }

        hash_value_ = get_hash (*this);
    }

    STPathElement (
        AccountID const& account, Currency const& currency,
        AccountID const& issuer, bool forceCurrency = false)
        : mType (typeNone), mAccountID (account), mCurrencyID (currency)
        , mIssuerID (issuer), is_offer_ (isXRP(mAccountID))
    {
        if (!is_offer_)
            mType |= typeAccount;

        if (forceCurrency || !isXRP(currency))
            mType |= typeCurrency;

        if (!isXRP(issuer))
            mType |= typeIssuer;

        hash_value_ = get_hash (*this);
    }

    STPathElement (
        unsigned int uType, AccountID const& account, Currency const& currency,
        AccountID const& issuer)
        : mType (uType), mAccountID (account), mCurrencyID (currency)
        , mIssuerID (issuer), is_offer_ (isXRP(mAccountID))
    {
        hash_value_ = get_hash (*this);
    }

    STPathElement ()
        : mType (typeNone), is_offer_ (true)
    {
        hash_value_ = get_hash (*this);
    }

    STPathElement(STPathElement const&) = default;
    STPathElement& operator=(STPathElement const&) = default;

    auto
    getNodeType () const
    {
        return mType;
    }

    bool
    isOffer () const
    {
        return is_offer_;
    }

    bool
    isAccount () const
    {
        return !isOffer ();
    }

    bool
    hasIssuer () const
    {
        return getNodeType () & STPathElement::typeIssuer;
    }

    bool
    hasCurrency () const
    {
        return getNodeType () & STPathElement::typeCurrency;
    }

    bool
    isNone () const
    {
        return getNodeType () == STPathElement::typeNone;
    }

    // Nodes are either an account ID or a offer prefix. Offer prefixs denote a
    // class of offers.
    AccountID const&
    getAccountID () const
    {
        return mAccountID;
    }

    Currency const&
    getCurrency () const
    {
        return mCurrencyID;
    }

    AccountID const&
    getIssuerID () const
    {
        return mIssuerID;
    }

    bool
    operator== (const STPathElement& t) const
    {
        return (mType & typeAccount) == (t.mType & typeAccount) &&
            hash_value_ == t.hash_value_ &&
            mAccountID == t.mAccountID &&
            mCurrencyID == t.mCurrencyID &&
            mIssuerID == t.mIssuerID;
    }

    bool
    operator!= (const STPathElement& t) const
    {
        return !operator==(t);
    }

private:
    unsigned int mType;
    AccountID mAccountID;
    Currency mCurrencyID;
    AccountID mIssuerID;

    bool is_offer_;
    std::size_t hash_value_;
};

class STPath
{
public:
    STPath () = default;

    STPath (std::vector<STPathElement> p)
        : mPath (std::move(p))
    { }

    std::vector<STPathElement>::size_type
    size () const
    {
        return mPath.size ();
    }

    bool empty() const
    {
        return mPath.empty ();
    }

    void
    push_back (STPathElement const& e)
    {
        mPath.push_back (e);
    }

    template <typename ...Args>
    void
    emplace_back (Args&&... args)
    {
        mPath.emplace_back (std::forward<Args> (args)...);
    }

    bool
    hasSeen (
        AccountID const& account, Currency const& currency,
        AccountID const& issuer) const;

    Json::Value
    getJson (JsonOptions) const;

    std::vector<STPathElement>::const_iterator
    begin () const
    {
        return mPath.begin ();
    }

    std::vector<STPathElement>::const_iterator
    end () const
    {
        return mPath.end ();
    }

    bool
    operator== (STPath const& t) const
    {
        return mPath == t.mPath;
    }

    std::vector<STPathElement>::const_reference
    back () const
    {
        return mPath.back ();
    }

    std::vector<STPathElement>::const_reference
    front () const
    {
        return mPath.front ();
    }

    STPathElement& operator[](int i)
    {
        return mPath[i];
    }

    const STPathElement& operator[](int i) const
    {
        return mPath[i];
    }

    void reserve(size_t s)
    {
        mPath.reserve(s);
    }
private:
    std::vector<STPathElement> mPath;
};

//------------------------------------------------------------------------------

// A set of zero or more payment paths
class STPathSet final
    : public STBase
{
public:
    STPathSet () = default;

    STPathSet (SField const& n)
        : STBase (n)
    { }

    STPathSet (SerialIter& sit, SField const& name);

    STBase*
    copy (std::size_t n, void* buf) const override
    {
        return emplace(n, buf, *this);
    }

    STBase*
    move (std::size_t n, void* buf) override
    {
        return emplace(n, buf, std::move(*this));
    }

    void
    add (Serializer& s) const override;

    Json::Value
    getJson (JsonOptions) const override;

    SerializedTypeID
    getSType () const override
    {
        return STI_PATHSET;
    }

    bool
    assembleAdd(STPath const& base, STPathElement const& tail);

    bool
    isEquivalent (const STBase& t) const override;

    bool
    isDefault () const override
    {
        return value.empty ();
    }

    // std::vector like interface:
    std::vector<STPath>::const_reference
    operator[] (std::vector<STPath>::size_type n) const
    {
        return value[n];
    }

    std::vector<STPath>::reference
    operator[] (std::vector<STPath>::size_type n)
    {
        return value[n];
    }

    std::vector<STPath>::const_iterator
    begin () const
    {
        return value.begin ();
    }

    std::vector<STPath>::const_iterator
    end () const
    {
        return value.end ();
    }

    std::vector<STPath>::size_type
    size () const
    {
        return value.size ();
    }

    bool
    empty () const
    {
        return value.empty ();
    }

    void
    push_back (STPath const& e)
    {
        value.push_back (e);
    }

    template <typename... Args>
    void emplace_back (Args&&... args)
    {
        value.emplace_back (std::forward<Args> (args)...);
    }

private:
    std::vector<STPath> value;
};

} // ripple

#endif
