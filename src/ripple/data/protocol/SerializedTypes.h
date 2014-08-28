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

#ifndef RIPPLE_SERIALIZEDTYPES_H
#define RIPPLE_SERIALIZEDTYPES_H

#include <ripple/data/protocol/SField.h>
#include <ripple/data/protocol/Serializer.h>
#include <ripple/data/protocol/STBitString.h>
#include <ripple/data/protocol/STInteger.h>
#include <ripple/data/protocol/SerializedType.h>
#include <ripple/data/protocol/STAmount.h>

namespace ripple {

//------------------------------------------------------------------------------

// variable length byte string
class STVariableLength : public SerializedType
{
public:
    STVariableLength (Blob const& v) : value (v)
    {
        ;
    }
    STVariableLength (SField::ref n, Blob const& v) : SerializedType (n), value (v)
    {
        ;
    }
    STVariableLength (SField::ref n) : SerializedType (n)
    {
        ;
    }
    STVariableLength (SerializerIterator&, SField::ref name = sfGeneric);
    STVariableLength ()
    {
        ;
    }
    static std::unique_ptr<SerializedType> deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    virtual SerializedTypeID getSType () const
    {
        return STI_VL;
    }
    virtual std::string getText () const;
    void add (Serializer& s) const
    {
        assert (fName->isBinary ());
        assert ((fName->fieldType == STI_VL) ||
            (fName->fieldType == STI_ACCOUNT));
        s.addVL (value);
    }

    Blob const& peekValue () const
    {
        return value;
    }
    Blob& peekValue ()
    {
        return value;
    }
    Blob getValue () const
    {
        return value;
    }
    void setValue (Blob const& v)
    {
        value = v;
    }

    operator Blob () const
    {
        return value;
    }
    virtual bool isEquivalent (const SerializedType& t) const;
    virtual bool isDefault () const
    {
        return value.empty ();
    }

private:
    Blob value;

    virtual STVariableLength* duplicate () const
    {
        return new STVariableLength (*this);
    }
    static STVariableLength* construct (SerializerIterator&, SField::ref);
};

//------------------------------------------------------------------------------

class STAccount : public STVariableLength
{
public:
    STAccount (Blob const& v) : STVariableLength (v)
    {
        ;
    }
    STAccount (SField::ref n, Blob const& v) : STVariableLength (n, v)
    {
        ;
    }
    STAccount (SField::ref n, Account const& v);
    STAccount (SField::ref n) : STVariableLength (n)
    {
        ;
    }
    STAccount ()
    {
        ;
    }
    static std::unique_ptr<SerializedType> deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    SerializedTypeID getSType () const
    {
        return STI_ACCOUNT;
    }
    std::string getText () const;

    RippleAddress getValueNCA () const;
    void setValueNCA (RippleAddress const& nca);

    template <typename Tag>
    void setValueH160 (base_uint<160, Tag> const& v)
    {
        peekValue ().clear ();
        peekValue ().insert (peekValue ().end (), v.begin (), v.end ());
        assert (peekValue ().size () == (160 / 8));
    }

    template <typename Tag>
    bool getValueH160 (base_uint<160, Tag>& v) const
    {
        auto success = isValueH160 ();
        if (success)
            memcpy (v.begin (), & (peekValue ().front ()), (160 / 8));
        return success;
    }

    bool isValueH160 () const;

private:
    virtual STAccount* duplicate () const
    {
        return new STAccount (*this);
    }
    static STAccount* construct (SerializerIterator&, SField::ref);
};

//------------------------------------------------------------------------------

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
    STPathElement (
        Account const& account, Currency const& currency,
        Account const& issuer, bool forceCurrency = false)
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
        unsigned int uType, Account const& account, Currency const& currency,
        Account const& issuer)
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

    int getNodeType () const
    {
        return mType;
    }
    bool isOffer () const
    {
        return is_offer_;
    }
    bool isAccount () const
    {
        return !isOffer ();
    }

    // Nodes are either an account ID or a offer prefix. Offer prefixs denote a
    // class of offers.
    Account const& getAccountID () const
    {
        return mAccountID;
    }
    Currency const& getCurrency () const
    {
        return mCurrencyID;
    }
    Account const& getIssuerID () const
    {
        return mIssuerID;
    }

    bool operator== (const STPathElement& t) const
    {
        return (mType & typeAccount) == (t.mType & typeAccount) &&
            hash_value_ == t.hash_value_ &&
            mAccountID == t.mAccountID &&
            mCurrencyID == t.mCurrencyID &&
            mIssuerID == t.mIssuerID;
    }

private:
    unsigned int mType;
    Account mAccountID;
    Currency mCurrencyID;
    Account mIssuerID;

    bool is_offer_;
    std::size_t hash_value_;
};

//------------------------------------------------------------------------------

class STPath
{
public:
    STPath () = default;

    STPath (std::vector<STPathElement> const& p)
        : mPath (p)
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

    bool hasSeen (Account const& account, Currency const& currency,
                  Account const& issuer) const;
    Json::Value getJson (int) const;

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

    bool operator== (STPath const& t) const
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

private:
    std::vector<STPathElement> mPath;
};

//------------------------------------------------------------------------------

// A set of zero or more payment paths
class STPathSet : public SerializedType
{
public:
    STPathSet () = default;

    explicit STPathSet (SField::ref n)
        : SerializedType (n)
    { }

    static
    std::unique_ptr<SerializedType>
    deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    void add (Serializer& s) const;
    virtual Json::Value getJson (int) const;

    SerializedTypeID getSType () const
    {
        return STI_PATHSET;
    }
    std::vector<STPath>::size_type
    size () const
    {
        return value.size ();
    }
    bool empty () const
    {
        return value.empty ();
    }
    void push_back (STPath const& e)
    {
        value.push_back (e);
    }

    bool assembleAdd(STPath const& base, STPathElement const& tail)
    { // assemble base+tail and add it to the set if it's not a duplicate
        value.push_back (base);

        std::vector<STPath>::reverse_iterator it = value.rbegin ();

        STPath& newPath = *it;
        newPath.push_back (tail);

        while (++it != value.rend ())
        {
            if (*it == newPath)
            {
                value.pop_back ();
                return false;
            }
        }
        return true;
    }

    virtual bool isEquivalent (const SerializedType& t) const;
    virtual bool isDefault () const
    {
        return value.empty ();
    }

    std::vector<STPath>::const_reference
    operator[] (std::vector<STPath>::size_type n) const
    {
        return value[n];
    }

    std::vector<STPath>::const_iterator begin () const
    {
        return value.begin ();
    }

    std::vector<STPath>::const_iterator end () const
    {
        return value.end ();
    }

private:
    std::vector<STPath> value;

    STPathSet (SField::ref n, const std::vector<STPath>& v)
        : SerializedType (n), value (v)
    { }

    STPathSet* duplicate () const
    {
        return new STPathSet (*this);
    }

    static
    STPathSet*
    construct (SerializerIterator&, SField::ref);
};

//------------------------------------------------------------------------------

class STVector256 : public SerializedType
{
public:
    STVector256 () = default;
    STVector256 (SField::ref n)
        : SerializedType (n)
    { }
    STVector256 (std::vector<uint256> const& vector)
        : mValue (vector)
    { }

    SerializedTypeID getSType () const
    {
        return STI_VECTOR256;
    }
    void add (Serializer& s) const;

    static
    std::unique_ptr<SerializedType>
    deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    const std::vector<uint256>&
    peekValue () const
    {
        return mValue;
    }
    std::vector<uint256>&
    peekValue ()
    {
        return mValue;
    }
    virtual bool isEquivalent (const SerializedType& t) const;
    virtual bool isDefault () const
    {
        return mValue.empty ();
    }

    std::vector<uint256>::size_type
    size () const
    {
        return mValue.size ();
    }
    bool empty () const
    {
        return mValue.empty ();
    }

    std::vector<uint256>::const_reference
    operator[] (std::vector<uint256>::size_type n) const
    {
        return mValue[n];
    }

    void setValue (const STVector256& v)
    {
         mValue = v.mValue;
    }
    void push_back (uint256 const& v)
    {
        mValue.push_back (v);
    }

    void sort ()
    {
        std::sort (mValue.begin (), mValue.end ());
    }

    Json::Value getJson (int) const;

    std::vector<uint256>::const_iterator
    begin() const
    {
        return mValue.begin ();
    }
    std::vector<uint256>::const_iterator
    end() const
    {
        return mValue.end ();
    }

private:
    std::vector<uint256>    mValue;

    STVector256* duplicate () const
    {
        return new STVector256 (*this);
    }
    static STVector256* construct (SerializerIterator&, SField::ref);
};

} // ripple

#endif
