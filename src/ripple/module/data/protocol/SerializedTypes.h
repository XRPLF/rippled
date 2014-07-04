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

#include <ripple/module/data/protocol/FieldNames.h>
#include <ripple/module/data/protocol/Serializer.h>
#include <ripple/module/data/protocol/SerializedType.h>
#include <ripple/module/data/protocol/STAmount.h>

namespace ripple {

// TODO(tom): make STUInt8, STUInt16, STUInt32, STUInt64 a single templated
// class to reduce the quadruple redundancy we have all over the rippled code
// regarding uint-like types.

class STUInt8 : public SerializedType
{
public:

    STUInt8 (unsigned char v = 0) : value (v)
    {
        ;
    }
    STUInt8 (SField::ref n, unsigned char v = 0) : SerializedType (n), value (v)
    {
        ;
    }
    static std::unique_ptr<SerializedType> deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    SerializedTypeID getSType () const
    {
        return STI_UINT8;
    }
    std::string getText () const;
    Json::Value getJson (int) const;
    void add (Serializer& s) const
    {
        assert (fName->isBinary ());
        assert (fName->fieldType == STI_UINT8);
        s.add8 (value);
    }

    unsigned char getValue () const
    {
        return value;
    }
    void setValue (unsigned char v)
    {
        value = v;
    }

    operator unsigned char () const
    {
        return value;
    }
    virtual bool isEquivalent (const SerializedType& t) const;
    virtual bool isDefault () const
    {
        return value == 0;
    }

private:
    unsigned char value;

    STUInt8* duplicate () const
    {
        return new STUInt8 (*this);
    }
    static STUInt8* construct (SerializerIterator&, SField::ref f);
};

//------------------------------------------------------------------------------

class STUInt16 : public SerializedType
{
public:

    STUInt16 (std::uint16_t v = 0) : value (v)
    {
        ;
    }
    STUInt16 (SField::ref n, std::uint16_t v = 0) : SerializedType (n), value (v)
    {
        ;
    }
    static std::unique_ptr<SerializedType> deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    SerializedTypeID getSType () const
    {
        return STI_UINT16;
    }
    std::string getText () const;
    Json::Value getJson (int) const;
    void add (Serializer& s) const
    {
        assert (fName->isBinary ());
        assert (fName->fieldType == STI_UINT16);
        s.add16 (value);
    }

    std::uint16_t getValue () const
    {
        return value;
    }
    void setValue (std::uint16_t v)
    {
        value = v;
    }

    operator std::uint16_t () const
    {
        return value;
    }
    virtual bool isEquivalent (const SerializedType& t) const;
    virtual bool isDefault () const
    {
        return value == 0;
    }

private:
    std::uint16_t value;

    STUInt16* duplicate () const
    {
        return new STUInt16 (*this);
    }
    static STUInt16* construct (SerializerIterator&, SField::ref name);
};

//------------------------------------------------------------------------------

class STUInt32 : public SerializedType
{
public:

    STUInt32 (std::uint32_t v = 0) : value (v)
    {
        ;
    }
    STUInt32 (SField::ref n, std::uint32_t v = 0) : SerializedType (n), value (v)
    {
        ;
    }
    static std::unique_ptr<SerializedType> deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    SerializedTypeID getSType () const
    {
        return STI_UINT32;
    }
    std::string getText () const;
    Json::Value getJson (int) const;
    void add (Serializer& s) const
    {
        assert (fName->isBinary ());
        assert (fName->fieldType == STI_UINT32);
        s.add32 (value);
    }

    std::uint32_t getValue () const
    {
        return value;
    }
    void setValue (std::uint32_t v)
    {
        value = v;
    }

    operator std::uint32_t () const
    {
        return value;
    }
    virtual bool isEquivalent (const SerializedType& t) const;
    virtual bool isDefault () const
    {
        return value == 0;
    }

private:
    std::uint32_t value;

    STUInt32* duplicate () const
    {
        return new STUInt32 (*this);
    }
    static STUInt32* construct (SerializerIterator&, SField::ref name);
};

//------------------------------------------------------------------------------

class STUInt64 : public SerializedType
{
public:
    STUInt64 (std::uint64_t v = 0) : value (v)
    {
        ;
    }
    STUInt64 (SField::ref n, std::uint64_t v = 0) : SerializedType (n), value (v)
    {
        ;
    }
    static std::unique_ptr<SerializedType> deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    SerializedTypeID getSType () const
    {
        return STI_UINT64;
    }
    std::string getText () const;
    Json::Value getJson (int) const;
    void add (Serializer& s) const
    {
        assert (fName->isBinary ());
        assert (fName->fieldType == STI_UINT64);
        s.add64 (value);
    }

    std::uint64_t getValue () const
    {
        return value;
    }
    void setValue (std::uint64_t v)
    {
        value = v;
    }

    operator std::uint64_t () const
    {
        return value;
    }
    virtual bool isEquivalent (const SerializedType& t) const;
    virtual bool isDefault () const
    {
        return value == 0;
    }

private:
    std::uint64_t value;

    STUInt64* duplicate () const
    {
        return new STUInt64 (*this);
    }
    static STUInt64* construct (SerializerIterator&, SField::ref name);
};

//------------------------------------------------------------------------------

// TODO(tom): make STHash128, STHash160 and STHash256 a single templated class
// to reduce the triple redundancy we have all over the rippled code regarding
// hash-like classes.

class STHash128 : public SerializedType
{
public:
    STHash128 (const uint128& v) : value (v)
    {
        ;
    }
    STHash128 (SField::ref n, const uint128& v) : SerializedType (n), value (v)
    {
        ;
    }
    STHash128 (SField::ref n, const char* v) : SerializedType (n)
    {
        value.SetHex (v);
    }
    STHash128 (SField::ref n, const std::string& v) : SerializedType (n)
    {
        value.SetHex (v);
    }
    STHash128 (SField::ref n) : SerializedType (n)
    {
        ;
    }
    STHash128 ()
    {
        ;
    }
    static std::unique_ptr<SerializedType> deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    SerializedTypeID getSType () const
    {
        return STI_HASH128;
    }
    virtual std::string getText () const;
    void add (Serializer& s) const
    {
        assert (fName->isBinary ());
        assert (fName->fieldType == STI_HASH128);
        s.add128 (value);
    }

    const uint128& getValue () const
    {
        return value;
    }
    void setValue (const uint128& v)
    {
        value = v;
    }

    operator uint128 () const
    {
        return value;
    }
    virtual bool isEquivalent (const SerializedType& t) const;
    virtual bool isDefault () const
    {
        return value.isZero ();
    }

private:
    uint128 value;

    STHash128* duplicate () const
    {
        return new STHash128 (*this);
    }
    static STHash128* construct (SerializerIterator&, SField::ref name);
};

//------------------------------------------------------------------------------

class STHash160 : public SerializedType
{
public:
    STHash160 (uint160 const& v) : value (v)
    {
        ;
    }
    STHash160 (SField::ref n, uint160 const& v) : SerializedType (n), value (v)
    {
        ;
    }
    STHash160 (SField::ref n, const char* v) : SerializedType (n)
    {
        value.SetHex (v);
    }
    STHash160 (SField::ref n, const std::string& v) : SerializedType (n)
    {
        value.SetHex (v);
    }
    STHash160 (SField::ref n) : SerializedType (n)
    {
        ;
    }
    STHash160 ()
    {
        ;
    }
    static std::unique_ptr<SerializedType> deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    SerializedTypeID getSType () const
    {
        return STI_HASH160;
    }
    virtual std::string getText () const;
    void add (Serializer& s) const
    {
        assert (fName->isBinary ());
        assert (fName->fieldType == STI_HASH160);
        s.add160 (value);
    }

    uint160 const& getValue () const
    {
        return value;
    }

    template <class Tag>
    void setValue (base_uint<160, Tag> const& v)
    {
        value.copyFrom(v);
    }

    operator uint160 () const
    {
        return value;
    }
    virtual bool isEquivalent (const SerializedType& t) const;
    virtual bool isDefault () const
    {
        return value.isZero ();
    }

private:
    uint160 value;

    STHash160* duplicate () const
    {
        return new STHash160 (*this);
    }
    static STHash160* construct (SerializerIterator&, SField::ref name);
};

//------------------------------------------------------------------------------

class STHash256 : public SerializedType
{
public:
    STHash256 (uint256 const& v) : value (v)
    {
        ;
    }
    STHash256 (SField::ref n, uint256 const& v) : SerializedType (n), value (v)
    {
        ;
    }
    STHash256 (SField::ref n, const char* v) : SerializedType (n)
    {
        value.SetHex (v);
    }
    STHash256 (SField::ref n, const std::string& v) : SerializedType (n)
    {
        value.SetHex (v);
    }
    STHash256 (SField::ref n) : SerializedType (n)
    {
        ;
    }
    STHash256 ()
    {
        ;
    }
    static std::unique_ptr<SerializedType> deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    SerializedTypeID getSType () const
    {
        return STI_HASH256;
    }
    std::string getText () const;
    void add (Serializer& s) const
    {
        assert (fName->isBinary ());
        assert (fName->fieldType == STI_HASH256);
        s.add256 (value);
    }

    uint256 const& getValue () const
    {
        return value;
    }
    void setValue (uint256 const& v)
    {
        value = v;
    }

    operator uint256 () const
    {
        return value;
    }
    virtual bool isEquivalent (const SerializedType& t) const;
    virtual bool isDefault () const
    {
        return value.isZero ();
    }

private:
    uint256 value;

    STHash256* duplicate () const
    {
        return new STHash256 (*this);
    }
    static STHash256* construct (SerializerIterator&, SField::ref);
};

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
    void setValueNCA (const RippleAddress& nca);

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
private:
    // VFALCO Remove these friend declarations
    friend class STPathSet;
    friend class STPath;
    friend class Pathfinder;

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

public:
    STPathElement (
        Account const& account, Currency const& currency,
        Account const& issuer, bool forceCurrency = false)
        : mAccountID (account), mCurrencyID (currency), mIssuerID (issuer)
    {
        mType   =
            (account.isZero () ? 0 : STPathElement::typeAccount)
            | ((currency.isZero () && !forceCurrency) ? 0 :
               STPathElement::typeCurrency)
            | (issuer.isZero () ? 0 : STPathElement::typeIssuer);
    }

    STPathElement (
        unsigned int uType, Account const& account, Currency const& currency,
        Account const& issuer)
        : mType (uType), mAccountID (account), mCurrencyID (currency),
          mIssuerID (issuer)
    {}

    STPathElement ()
        : mType (0)
    {}

    int getNodeType () const
    {
        return mType;
    }
    bool isOffer () const
    {
        return mAccountID.isZero ();
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
        return (mType & typeAccount) == (t.mType & typeAccount)
                && mAccountID == t.mAccountID
                && mCurrencyID == t.mCurrencyID
                && mIssuerID == t.mIssuerID;
    }

private:
    unsigned int mType;
    Account mAccountID;
    Currency mCurrencyID;
    Account mIssuerID;
};

//------------------------------------------------------------------------------

class STPath
{
public:
    STPath ()
    {
        ;
    }
    STPath (const std::vector<STPathElement>& p) : mPath (p)
    {
        ;
    }

    int size () const
    {
        return mPath.size ();
    }
    void reserve (size_t n)
    {
        mPath.reserve(n);
    }
    bool isEmpty () const
    {
        return mPath.empty ();
    }
    bool empty() const
    {
        return mPath.empty ();
    }

    const STPathElement& getElement (int offset) const
    {
        return mPath[offset];
    }
    const STPathElement& getElement (int offset)
    {
        return mPath[offset];
    }
    void addElement (const STPathElement& e)
    {
        mPath.push_back (e);
    }
    void clear ()
    {
        mPath.clear ();
    }
    bool hasSeen (Account const& account, Currency const& currency,
                  Account const& issuer) const;
    Json::Value getJson (int) const;

    std::vector<STPathElement>::iterator begin ()
    {
        return mPath.begin ();
    }
    std::vector<STPathElement>::iterator end ()
    {
        return mPath.end ();
    }
    std::vector<STPathElement>::const_iterator begin () const
    {
        return mPath.begin ();
    }
    std::vector<STPathElement>::const_iterator end () const
    {
        return mPath.end ();
    }

    bool operator== (const STPath& t) const
    {
        return mPath == t.mPath;
    }

    void setCanonical (const STPath& spExpanded);

private:
    friend class STPathSet;
    friend class Pathfinder;

    std::vector<STPathElement> mPath;
};

inline std::vector<STPathElement>::iterator range_begin (STPath& x)
{
    return x.begin ();
}

inline std::vector<STPathElement>::iterator range_end (STPath& x)
{
    return x.end ();
}

inline std::vector<STPathElement>::const_iterator range_begin (const STPath& x)
{
    return x.begin ();
}

inline std::vector<STPathElement>::const_iterator range_end (const STPath& x)
{
    return x.end ();
}

//------------------------------------------------------------------------------

// A set of zero or more payment paths
class STPathSet : public SerializedType
{
public:
    STPathSet ()
    {
        ;
    }

    explicit STPathSet (SField::ref n) : SerializedType (n)
    {
        ;
    }

    explicit STPathSet (const std::vector<STPath>& v) : value (v)
    {
        ;
    }

    STPathSet (SField::ref n, const std::vector<STPath>& v) : SerializedType (n), value (v)
    {
        ;
    }

    static std::unique_ptr<SerializedType> deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    //  std::string getText() const;
    void add (Serializer& s) const;
    virtual Json::Value getJson (int) const;

    SerializedTypeID getSType () const
    {
        return STI_PATHSET;
    }
    int size () const
    {
        return value.size ();
    }
    void reserve (size_t n)
    {
        value.reserve(n);
    }
    const STPath& getPath (int off) const
    {
        return value[off];
    }
    STPath& peekPath (int off)
    {
        return value[off];
    }
    bool isEmpty () const
    {
        return value.empty ();
    }
    void clear ()
    {
        value.clear ();
    }
    void addPath (const STPath& e)
    {
        value.push_back (e);
    }
    void addUniquePath (const STPath& e)
    {
        for (auto const& p: value)
        {
            if (p == e)
                return;
        }
        value.push_back (e);
    }

    bool assembleAdd(STPath const& base, STPathElement const& tail)
    { // assemble base+tail and add it to the set if it's not a duplicate
        value.push_back (base);

        std::vector<STPath>::reverse_iterator it = value.rbegin ();

        STPath& newPath = *it;
        newPath.mPath.push_back (tail);

        while (++it != value.rend ())
        {
            if (it->mPath == newPath.mPath)
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

    STPath& operator[](size_t n)
    {
        return value[n];
    }
    STPath const& operator[](size_t n) const
    {
        return value[n];
    }

    std::vector<STPath>::iterator begin ()
    {
        return value.begin ();
    }
    std::vector<STPath>::iterator end ()
    {
        return value.end ();
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

    STPathSet* duplicate () const
    {
        return new STPathSet (*this);
    }
    static STPathSet* construct (SerializerIterator&, SField::ref);
};

inline std::vector<STPath>::iterator range_begin (STPathSet& x)
{
    return x.begin ();
}

inline std::vector<STPath>::iterator range_end (STPathSet& x)
{
    return x.end ();
}

inline std::vector<STPath>::const_iterator range_begin (const STPathSet& x)
{
    return x.begin ();
}

inline std::vector<STPath>::const_iterator range_end (const STPathSet& x)
{
    return x.end ();
}

//------------------------------------------------------------------------------

class STVector256 : public SerializedType
{
public:
    STVector256 ()
    {
        ;
    }
    STVector256 (SField::ref n) : SerializedType (n)
    {
        ;
    }
    STVector256 (SField::ref n, const std::vector<uint256>& v) : SerializedType (n), mValue (v)
    {
        ;
    }
    STVector256 (const std::vector<uint256>& vector) : mValue (vector)
    {
        ;
    }

    SerializedTypeID getSType () const
    {
        return STI_VECTOR256;
    }
    void add (Serializer& s) const;

    static std::unique_ptr<SerializedType> deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    const std::vector<uint256>& peekValue () const
    {
        return mValue;
    }
    std::vector<uint256>& peekValue ()
    {
        return mValue;
    }
    virtual bool isEquivalent (const SerializedType& t) const;
    virtual bool isDefault () const
    {
        return mValue.empty ();
    }

    std::vector<uint256> getValue () const
    {
        return mValue;
    }
    int size () const
    {
        return mValue.size ();
    }
    bool isEmpty () const
    {
        return mValue.empty ();
    }

    uint256 const& at (int i) const
    {
        assert ((i >= 0) && (i < size ()));
        return mValue.at (i);
    }
    uint256& at (int i)
    {
        assert ((i >= 0) && (i < size ()));
        return mValue.at (i);
    }

    void setValue (const STVector256& v)
    {
        mValue = v.mValue;
    }
    void setValue (const std::vector<uint256>& v)
    {
        mValue = v;
    }
    void addValue (uint256 const& v)
    {
        mValue.push_back (v);
    }
    bool hasValue (uint256 const& v) const;
    void sort ()
    {
        std::sort (mValue.begin (), mValue.end ());
    }

    Json::Value getJson (int) const;

    std::vector<uint256>::const_iterator begin() const
    {
        return mValue.begin ();
    }
    std::vector<uint256>::const_iterator end() const
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
