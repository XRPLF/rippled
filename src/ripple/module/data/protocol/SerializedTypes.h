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

namespace ripple {

// VFALCO TODO fix this restriction on copy assignment.
//
// CAUTION: Do not create a vector (or similar container) of any object derived from
// SerializedType. Use Boost ptr_* containers. The copy assignment operator of
// SerializedType has semantics that will cause contained types to change their names
// when an object is deleted because copy assignment is used to "slide down" the
// remaining types and this will not copy the field name. Changing the copy assignment
// operator to copy the field name breaks the use of copy assignment just to copy values,
// which is used in the transaction engine code.

// VFALCO TODO Remove this unused enum
/*
enum PathFlags
{
    PF_END              = 0x00,     // End of current path & path list.
    PF_BOUNDARY         = 0xFF,     // End of current path & new path follows.

    PF_ACCOUNT          = 0x01,
    PF_OFFER            = 0x02,

    PF_WANTED_CURRENCY  = 0x10,
    PF_WANTED_ISSUER    = 0x20,
    PF_REDEEM           = 0x40,
    PF_ISSUE            = 0x80,
};
*/

// VFALCO TODO make these non static or otherwise clean constants.
static const uint160 u160_zero (0), u160_one (1);
static inline const uint160& get_u160_zero ()
{
    return u160_zero;
}
static inline const uint160& get_u160_one ()
{
    return u160_one;
}

// VFALCO TODO replace these with language constructs
#define CURRENCY_XRP        get_u160_zero()
#define CURRENCY_ONE        get_u160_one()                  // Used as a place holder.
#define CURRENCY_BAD        uint160(0x5852500000000000)     // Do not allow XRP as an IOU currency.
#define ACCOUNT_XRP         get_u160_zero()
#define ACCOUNT_ONE         get_u160_one()                  // Used as a place holder.

//------------------------------------------------------------------------------

/** A type which can be exported to a well known binary format.

    A SerializedType:
        - Always a field
        - Can always go inside an eligible enclosing SerializedType
            (such as STArray)
        - Has a field name


    Like JSON, a SerializedObject is a basket which has rules
    on what it can hold.
*/
// VFALCO TODO Document this as it looks like a central class.
//             STObject is derived from it
//
class SerializedType
{
public:
    SerializedType () : fName (&sfGeneric)
    {
        ;
    }

    explicit SerializedType (SField::ref n) : fName (&n)
    {
        assert (fName);
    }

    virtual ~SerializedType () { }

    static std::unique_ptr<SerializedType> deserialize (SField::ref name)
    {
        return std::unique_ptr<SerializedType> (new SerializedType (name));
    }

    /** A SerializeType is a field.
        This sets the name.
    */
    void setFName (SField::ref n)
    {
        fName = &n;
        assert (fName);
    }
    SField::ref getFName () const
    {
        return *fName;
    }
    std::string getName () const
    {
        return fName->fieldName;
    }
    Json::StaticString const& getJsonName () const
    {
        return fName->getJsonName ();
    }

    virtual SerializedTypeID getSType () const
    {
        return STI_NOTPRESENT;
    }
    std::unique_ptr<SerializedType> clone () const
    {
        return std::unique_ptr<SerializedType> (duplicate ());
    }

    virtual std::string getFullText () const;
    virtual std::string getText () const // just the value
    {
        return std::string ();
    }
    virtual Json::Value getJson (int /*options*/) const
    {
        return getText ();
    }

    virtual void add (Serializer& s) const
    {
        ;
    }

    virtual bool isEquivalent (const SerializedType& t) const;

    void addFieldID (Serializer& s) const
    {
        s.addFieldID (fName->fieldType, fName->fieldValue);
    }

    SerializedType& operator= (const SerializedType& t);

    bool operator== (const SerializedType& t) const
    {
        return (getSType () == t.getSType ()) && isEquivalent (t);
    }
    bool operator!= (const SerializedType& t) const
    {
        return (getSType () != t.getSType ()) || !isEquivalent (t);
    }

    virtual bool isDefault () const
    {
        return true;
    }

    template <class D>
    D&  downcast()
    {
        D* ptr = dynamic_cast<D*> (this);
        if (ptr == nullptr)
            throw std::runtime_error ("type mismatch");
        return *ptr;
    }

    template <class D>
    D const& downcast() const
    {
        D const * ptr = dynamic_cast<D const*> (this);
        if (ptr == nullptr)
            throw std::runtime_error ("type mismatch");
        return *ptr;
    }

protected:
    // VFALCO TODO make accessors for this
    SField::ptr fName;

private:
    virtual SerializedType* duplicate () const
    {
        return new SerializedType (*fName);
    }
};

//------------------------------------------------------------------------------

inline SerializedType* new_clone (const SerializedType& s)
{
    SerializedType* const copy (s.clone ().release ());
    assert (typeid (*copy) == typeid (s));
    return copy;
}

inline void delete_clone (const SerializedType* s)
{
    boost::checked_delete (s);
}

inline std::ostream& operator<< (std::ostream& out, const SerializedType& t)
{
    return out << t.getFullText ();
}

//------------------------------------------------------------------------------

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

// Internal form:
// 1: If amount is zero, then value is zero and offset is -100
// 2: Otherwise:
//   legal offset range is -96 to +80 inclusive
//   value range is 10^15 to (10^16 - 1) inclusive
//  amount = value * [10 ^ offset]

// Wire form:
// High 8 bits are (offset+142), legal range is, 80 to 22 inclusive
// Low 56 bits are value, legal range is 10^15 to (10^16 - 1) inclusive
class STAmount : public SerializedType
{
public:
    static const int cMinOffset            = -96, cMaxOffset = 80;
    static const std::uint64_t cMinValue   = 1000000000000000ull, cMaxValue = 9999999999999999ull;
    static const std::uint64_t cMaxNative  = 9000000000000000000ull;
    static const std::uint64_t cMaxNativeN = 100000000000000000ull; // max native value on network
    static const std::uint64_t cNotNative  = 0x8000000000000000ull;
    static const std::uint64_t cPosNative  = 0x4000000000000000ull;

    static std::uint64_t   uRateOne;

    STAmount (std::uint64_t v = 0, bool isNeg = false) : mValue (v), mOffset (0), mIsNative (true), mIsNegative (isNeg)
    {
        if (v == 0) mIsNegative = false;
    }

    STAmount (SField::ref n, std::uint64_t v = 0, bool isNeg = false)
        : SerializedType (n), mValue (v), mOffset (0), mIsNative (true), mIsNegative (isNeg)
    {
        ;
    }

    STAmount (SField::ref n, std::int64_t v) : SerializedType (n), mOffset (0), mIsNative (true)
    {
        set (v);
    }

    STAmount (const uint160& currency, const uint160& issuer,
              std::uint64_t uV = 0, int iOff = 0, bool bNegative = false)
        : mCurrency (currency), mIssuer (issuer), mValue (uV), mOffset (iOff), mIsNegative (bNegative)
    {
        canonicalize ();
    }

    STAmount (const uint160& currency, const uint160& issuer,
              std::uint32_t uV, int iOff = 0, bool bNegative = false)
        : mCurrency (currency), mIssuer (issuer), mValue (uV), mOffset (iOff), mIsNegative (bNegative)
    {
        canonicalize ();
    }

    STAmount (SField::ref n, const uint160& currency, const uint160& issuer,
              std::uint64_t v = 0, int off = 0, bool isNeg = false) :
        SerializedType (n), mCurrency (currency), mIssuer (issuer), mValue (v), mOffset (off), mIsNegative (isNeg)
    {
        canonicalize ();
    }

    STAmount (const uint160& currency, const uint160& issuer, std::int64_t v, int iOff = 0)
        : mCurrency (currency), mIssuer (issuer), mOffset (iOff)
    {
        set (v);
        canonicalize ();
    }

    STAmount (SField::ref n, const uint160& currency, const uint160& issuer, std::int64_t v, int off = 0)
        : SerializedType (n), mCurrency (currency), mIssuer (issuer), mOffset (off)
    {
        set (v);
        canonicalize ();
    }

    STAmount (const uint160& currency, const uint160& issuer, int v, int iOff = 0)
        : mCurrency (currency), mIssuer (issuer), mOffset (iOff)
    {
        set (v);
        canonicalize ();
    }

    STAmount (SField::ref n, const uint160& currency, const uint160& issuer, int v, int off = 0)
        : SerializedType (n), mCurrency (currency), mIssuer (issuer), mOffset (off)
    {
        set (v);
        canonicalize ();
    }

    STAmount (SField::ref, const Json::Value&);

    static STAmount createFromInt64 (SField::ref n, std::int64_t v);

    static std::unique_ptr<SerializedType> deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    bool bSetJson (const Json::Value& jvSource);

    static STAmount saFromRate (std::uint64_t uRate = 0)
    {
        return STAmount (CURRENCY_ONE, ACCOUNT_ONE, uRate, -9, false);
    }

    SerializedTypeID getSType () const
    {
        return STI_AMOUNT;
    }
    std::string getText () const;
    std::string getFullText () const;
    void add (Serializer& s) const;

    int getExponent () const
    {
        return mOffset;
    }
    std::uint64_t getMantissa () const
    {
        return mValue;
    }

    int signum () const
    {
        return mValue ? (mIsNegative ? -1 : 1) : 0;
    }

    // When the currency is XRP, the value in raw units. S=signed
    std::uint64_t getNValue () const
    {
        if (!mIsNative) throw std::runtime_error ("not native");

        return mValue;
    }
    void setNValue (std::uint64_t v)
    {
        if (!mIsNative) throw std::runtime_error ("not native");

        mValue = v;
    }
    std::int64_t getSNValue () const;
    void setSNValue (std::int64_t);

    std::string getHumanCurrency () const;

    bool isNative () const
    {
        return mIsNative;
    }
    bool isLegalNet () const
    {
        return !mIsNative || (mValue <= cMaxNativeN);
    }

    explicit
    operator bool () const noexcept
    {
        return *this != zero;
    }

    void negate ()
    {
        if (*this != zero)
            mIsNegative = !mIsNegative;
    }

    // Return a copy of amount with the same Issuer and Currency but zero value.
    STAmount zeroed() const
    {
        STAmount c(mCurrency, mIssuer);
        c = zero;
        // See https://ripplelabs.atlassian.net/browse/WC-1847?jql=
        return c;
    }

    void clear ()
    {
        // VFALCO: Why -100?
        mOffset = mIsNative ? 0 : -100;
        mValue = 0;
        mIsNegative = false;
    }

    // Zero while copying currency and issuer.
    void clear (const STAmount& saTmpl)
    {
        mCurrency = saTmpl.mCurrency;
        mIssuer = saTmpl.mIssuer;
        mIsNative = saTmpl.mIsNative;
        clear ();
    }
    void clear (const uint160& currency, const uint160& issuer)
    {
        mCurrency = currency;
        mIssuer = issuer;
        mIsNative = !currency;
        clear ();
    }

    STAmount& operator=(beast::Zero)
    {
        clear ();
        return *this;
    }

    int compare (const STAmount&) const;

    const uint160& getIssuer () const
    {
        return mIssuer;
    }
    STAmount* setIssuer (const uint160& uIssuer)
    {
        mIssuer   = uIssuer;
        return this;
    }

    const uint160& getCurrency () const
    {
        return mCurrency;
    }
    bool setValue (const std::string& sAmount);
    bool setFullValue (const std::string& sAmount, const std::string& sCurrency = "", const std::string& sIssuer = "");
    void setValue (const STAmount&);

    virtual bool isEquivalent (const SerializedType& t) const;
    virtual bool isDefault () const
    {
        return (mValue == 0) && mIssuer.isZero () && mCurrency.isZero ();
    }

    bool operator== (const STAmount&) const;
    bool operator!= (const STAmount&) const;
    bool operator< (const STAmount&) const;
    bool operator> (const STAmount&) const;
    bool operator<= (const STAmount&) const;
    bool operator>= (const STAmount&) const;
    bool isComparable (const STAmount&) const;
    void throwComparable (const STAmount&) const;

    // native currency only
    bool operator< (std::uint64_t) const;
    bool operator> (std::uint64_t) const;
    bool operator<= (std::uint64_t) const;
    bool operator>= (std::uint64_t) const;
    STAmount operator+ (std::uint64_t) const;
    STAmount operator- (std::uint64_t) const;
    STAmount operator- (void) const;

    STAmount& operator+= (const STAmount&);
    STAmount& operator-= (const STAmount&);
    STAmount& operator+= (std::uint64_t);
    STAmount& operator-= (std::uint64_t);
    STAmount& operator= (std::uint64_t);

    operator double () const;

    friend STAmount operator+ (const STAmount& v1, const STAmount& v2);
    friend STAmount operator- (const STAmount& v1, const STAmount& v2);

    static STAmount divide (
        const STAmount& v1, const STAmount& v2,
        const uint160& currency, const uint160& issuer);

    static STAmount divide (
        const STAmount& v1, const STAmount& v2, const STAmount& saUnit)
    {
        return divide (v1, v2, saUnit.getCurrency (), saUnit.getIssuer ());
    }
    static STAmount divide (const STAmount& v1, const STAmount& v2)
    {
        return divide (v1, v2, v1);
    }

    static STAmount multiply (
        const STAmount& v1, const STAmount& v2,
        const uint160& currency, const uint160& issuer);

    static STAmount multiply (
        const STAmount& v1, const STAmount& v2, const STAmount& saUnit)
    {
        return multiply (v1, v2, saUnit.getCurrency (), saUnit.getIssuer ());
    }
    static STAmount multiply (const STAmount& v1, const STAmount& v2)
    {
        return multiply (v1, v2, v1);
    }

    /* addRound, subRound can end up rounding if the amount subtracted is too small
       to make a change. Consder (X-d) where d is very small relative to X.
       If you ask to round down, then (X-d) should not be X unless d is zero.
       If you ask to round up, (X+d) should never be X unless d is zero. (Assuming X and d are positive).
    */
    // Add, subtract, multiply, or divide rounding result in specified direction
    static STAmount addRound (const STAmount& v1, const STAmount& v2, bool roundUp);
    static STAmount subRound (const STAmount& v1, const STAmount& v2, bool roundUp);
    static STAmount mulRound (const STAmount& v1, const STAmount& v2,
                              const uint160& currency, const uint160& issuer, bool roundUp);
    static STAmount divRound (const STAmount& v1, const STAmount& v2,
                              const uint160& currency, const uint160& issuer, bool roundUp);

    static STAmount mulRound (const STAmount& v1, const STAmount& v2, const STAmount& saUnit, bool roundUp)
    {
        return mulRound (v1, v2, saUnit.getCurrency (), saUnit.getIssuer (), roundUp);
    }
    static STAmount mulRound (const STAmount& v1, const STAmount& v2, bool roundUp)
    {
        return mulRound (v1, v2, v1.getCurrency (), v1.getIssuer (), roundUp);
    }
    static STAmount divRound (const STAmount& v1, const STAmount& v2, const STAmount& saUnit, bool roundUp)
    {
        return divRound (v1, v2, saUnit.getCurrency (), saUnit.getIssuer (), roundUp);
    }
    static STAmount divRound (const STAmount& v1, const STAmount& v2, bool roundUp)
    {
        return divRound (v1, v2, v1.getCurrency (), v1.getIssuer (), roundUp);
    }

    // Someone is offering X for Y, what is the rate?
    // Rate: smaller is better, the taker wants the most out: in/out
    static std::uint64_t getRate (const STAmount& offerOut, const STAmount& offerIn);
    static STAmount setRate (std::uint64_t rate);

    // Someone is offering X for Y, I need Z, how much do I pay
    static STAmount getPay (const STAmount& offerOut, const STAmount& offerIn, const STAmount& needed);

    static std::string createHumanCurrency (const uint160& uCurrency);
    static Json::Value createJsonCurrency (const uint160& uCurrency)
    // XXX Punted.
    {
        return createHumanCurrency (uCurrency);
    }

    static STAmount deserialize (SerializerIterator&);
    static bool currencyFromString (uint160& uDstCurrency, const std::string& sCurrency);
    static bool issuerFromString (uint160& uDstIssuer, const std::string& sIssuer);

    Json::Value getJson (int) const;
    void setJson (Json::Value&) const;

    STAmount getRound () const;
    void roundSelf ();

    static void canonicalizeRound (bool isNative, std::uint64_t& value, int& offset, bool roundUp);

private:
    uint160 mCurrency;      // Compared by ==. Always update mIsNative.
    uint160 mIssuer;        // Not compared by ==. 0 for XRP.

    std::uint64_t  mValue;
    int            mOffset;
    bool           mIsNative;      // Always !mCurrency. Native is XRP.
    bool           mIsNegative;

    void canonicalize ();
    STAmount* duplicate () const
    {
        return new STAmount (*this);
    }
    static STAmount* construct (SerializerIterator&, SField::ref name);

    STAmount (SField::ref name, const uint160& cur, const uint160& iss,
              std::uint64_t val, int off, bool isNat, bool isNeg)
        : SerializedType (name), mCurrency (cur), mIssuer (iss),  mValue (val), mOffset (off),
          mIsNative (isNat), mIsNegative (isNeg)
    {
        ;
    }

    void set (std::int64_t v)
    {
        if (v < 0)
        {
            mIsNegative = true;
            mValue = static_cast<std::uint64_t> (-v);
        }
        else
        {
            mIsNegative = false;
            mValue = static_cast<std::uint64_t> (v);
        }
    }

    void set (int v)
    {
        if (v < 0)
        {
            mIsNegative = true;
            mValue = static_cast<std::uint64_t> (-v);
        }
        else
        {
            mIsNegative = false;
            mValue = static_cast<std::uint64_t> (v);
        }
    }
};

// VFALCO TODO Make static member accessors for these in STAmount
extern const STAmount saZero;
extern const STAmount saOne;

//------------------------------------------------------------------------------

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
    STHash160 (const uint160& v) : value (v)
    {
        ;
    }
    STHash160 (SField::ref n, const uint160& v) : SerializedType (n), value (v)
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
        s.add160 (value);
    }

    const uint160& getValue () const
    {
        return value;
    }
    void setValue (const uint160& v)
    {
        value = v;
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
    STAccount (SField::ref n, const uint160& v);
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

    void setValueH160 (const uint160& v);
    bool getValueH160 (uint160&) const;
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
        const uint160& account, const uint160& currency,
        const uint160& issuer, bool forceCurrency = false)
        : mAccountID (account), mCurrencyID (currency), mIssuerID (issuer)
    {
        mType   =
            (account.isZero () ? 0 : STPathElement::typeAccount)
            | ((currency.isZero () && !forceCurrency) ? 0 :
               STPathElement::typeCurrency)
            | (issuer.isZero () ? 0 : STPathElement::typeIssuer);
    }

    STPathElement (
        unsigned int uType, const uint160& account, const uint160& currency,
        const uint160& issuer)
        : mType (uType), mAccountID (account), mCurrencyID (currency),
          mIssuerID (issuer)
    {
        ;
    }

    STPathElement ()
        : mType (0)
    {
        ;
    }

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

    // Nodes are either an account ID or a offer prefix. Offer prefixs denote a class of offers.
    const uint160& getAccountID () const
    {
        return mAccountID;
    }
    const uint160& getCurrency () const
    {
        return mCurrencyID;
    }
    const uint160& getIssuerID () const
    {
        return mIssuerID;
    }

    bool operator== (const STPathElement& t) const
    {
        return ((mType & typeAccount) == (t.mType & typeAccount))
                && (mAccountID == t.mAccountID)
                && (mCurrencyID == t.mCurrencyID)
                && (mIssuerID == t.mIssuerID);
    }

private:
    unsigned int    mType;
    uint160         mAccountID;
    uint160         mCurrencyID;
    uint160         mIssuerID;
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

    void printDebug ();
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
    bool hasSeen (const uint160& account, const uint160& currency,
                  const uint160& issuer) const;
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
        BOOST_FOREACH(const STPath& p, value)
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

    void printDebug ();

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
