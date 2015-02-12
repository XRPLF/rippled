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

#ifndef RIPPLE_PROTOCOL_STOBJECT_H_INCLUDED
#define RIPPLE_PROTOCOL_STOBJECT_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STPathSet.h>
#include <ripple/protocol/STVector256.h>
#include <ripple/protocol/SOTemplate.h>
#include <boost/ptr_container/ptr_vector.hpp>

namespace ripple {

class STArray;

class STObject
    : public STBase
    , public CountedObject <STObject>
{
public:
    static char const* getCountedObjectName () { return "STObject"; }

    STObject () : mType (nullptr)
    {
        ;
    }

    explicit STObject (SField::ref name)
        : STBase (name), mType (nullptr)
    {
        ;
    }

    STObject (const SOTemplate & type, SField::ref name)
        : STBase (name)
    {
        set (type);
    }

    STObject (
        const SOTemplate & type, SerialIter & sit, SField::ref name)
        : STBase (name)
    {
        set (sit);
        setType (type);
    }

    STObject (SField::ref name, boost::ptr_vector<STBase>& data)
        : STBase (name), mType (nullptr)
    {
        mData.swap (data);
    }

    std::unique_ptr <STObject> oClone () const
    {
        return std::make_unique <STObject> (*this);
    }

    virtual ~STObject () { }

    static std::unique_ptr<STBase>
    deserialize (SerialIter & sit, SField::ref name);

    bool setType (const SOTemplate & type);
    bool isValidForType ();
    bool isFieldAllowed (SField::ref);
    bool isFree () const
    {
        return mType == nullptr;
    }

    void set (const SOTemplate&);
    bool set (SerialIter& u, int depth = 0);

    virtual SerializedTypeID getSType () const override
    {
        return STI_OBJECT;
    }
    virtual bool isEquivalent (const STBase & t) const override;
    virtual bool isDefault () const override
    {
        return mData.empty ();
    }

    virtual void add (Serializer & s) const override
    {
        add (s, true);    // just inner elements
    }

    void add (Serializer & s, bool withSignature) const;

    // VFALCO NOTE does this return an expensive copy of an object with a
    //             dynamic buffer?
    // VFALCO TODO Remove this function and fix the few callers.
    Serializer getSerializer () const
    {
        Serializer s;
        add (s);
        return s;
    }

    virtual std::string getFullText () const override;
    virtual std::string getText () const override;

    // TODO(tom): options should be an enum.
    virtual Json::Value getJson (int options) const override;

    int addObject (const STBase & t)
    {
        mData.push_back (t.duplicate ().release ());
        return mData.size () - 1;
    }
    int giveObject (std::unique_ptr<STBase> t)
    {
        mData.push_back (t.release ());
        return mData.size () - 1;
    }
    int giveObject (STBase * t)
    {
        mData.push_back (t);
        return mData.size () - 1;
    }
    const boost::ptr_vector<STBase>& peekData () const
    {
        return mData;
    }
    boost::ptr_vector<STBase>& peekData ()
    {
        return mData;
    }
    STBase& front ()
    {
        return mData.front ();
    }
    const STBase& front () const
    {
        return mData.front ();
    }
    STBase& back ()
    {
        return mData.back ();
    }
    const STBase& back () const
    {
        return mData.back ();
    }

    int getCount () const
    {
        return mData.size ();
    }

    bool setFlag (std::uint32_t);
    bool clearFlag (std::uint32_t);
    bool isFlag(std::uint32_t) const;
    std::uint32_t getFlags () const;

    uint256 getHash (std::uint32_t prefix) const;
    uint256 getSigningHash (std::uint32_t prefix) const;

    const STBase& peekAtIndex (int offset) const
    {
        return mData[offset];
    }
    STBase& getIndex (int offset)
    {
        return mData[offset];
    }
    const STBase* peekAtPIndex (int offset) const
    {
        return & (mData[offset]);
    }
    STBase* getPIndex (int offset)
    {
        return & (mData[offset]);
    }

    int getFieldIndex (SField::ref field) const;
    SField::ref getFieldSType (int index) const;

    const STBase& peekAtField (SField::ref field) const;
    STBase& getField (SField::ref field);
    const STBase* peekAtPField (SField::ref field) const;
    STBase* getPField (SField::ref field, bool createOkay = false);

    // these throw if the field type doesn't match, or return default values
    // if the field is optional but not present
    std::string getFieldString (SField::ref field) const;
    unsigned char getFieldU8 (SFieldU8 const& /*SField::ref*/ field) const;
    std::uint16_t getFieldU16 (SFieldU16 const& /*SField::ref*/ field) const;
    std::uint32_t getFieldU32 (SFieldU32 const& /*SField::ref*/ field) const;
    std::uint64_t getFieldU64 (SFieldU64 const& /*SField::ref*/ field) const;
    uint128 getFieldH128 (SFieldH128 const& /*SField::ref*/ field) const;

    uint160 getFieldH160 (SFieldH160 const& /*SField::ref*/ field) const;
    uint256 getFieldH256 (SFieldH256 const& /*SField::ref*/ field) const;
    RippleAddress getFieldAccount (SFieldAccount const& /*SField::ref*/ field) const;
    Account getFieldAccount160 (SFieldAccount const& /*SField::ref*/ field) const;

    Blob getFieldVL (SFieldVL const& /*SField::ref*/ field) const;
    STAmount const& getFieldAmount (SFieldAmount const& /*SField::ref*/ field) const;
    STPathSet const& getFieldPathSet (SFieldPathSet const& /*SField::ref*/ field) const;
    const STVector256& getFieldV256 (SFieldV256 const& /*SField::ref*/ field) const;
    const STArray& getFieldArray (SFieldArray const& /*SField::ref*/  field) const;

    void setFieldU8 (SFieldU8 const&, unsigned char);
    void setFieldU16 (SFieldU16 const&, std::uint16_t);
    void setFieldU32 (SFieldU32 const&, std::uint32_t);
    void setFieldU64 (SFieldU64 const&, std::uint64_t);
    void setFieldH128 (SFieldH128 const&, uint128 const&);
    void setFieldH256 (SFieldH256 const&, uint256 const& );
    void setFieldVL (SFieldVL const&, Blob const&);
    void setFieldAccount (SFieldAccount const&, Account const&);
    void setFieldAccount (SFieldAccount const& field, RippleAddress const& addr)
    {
        setFieldAccount (field, addr.getAccountID ());
    }
    void setFieldAmount (SFieldAmount const&, STAmount const&);
    void setFieldPathSet (SFieldPathSet const&, STPathSet const&);
    void setFieldV256 (SFieldV256 const&, STVector256 const& v);
    void setFieldArray (SFieldArray const&, STArray const& v);

    template <class Tag>
    void setFieldH160 (SFieldH160 const& field, base_uint<160, Tag> const& v)
    {
        STBase* rf = getPField (field, true);

        if (!rf)
            throw std::runtime_error ("Field not found");

        if (rf->getSType () == STI_NOTPRESENT)
            rf = makeFieldPresent (field);

        using Bits = STBitString<160>;
        if (auto cf = dynamic_cast<Bits*> (rf))
            cf->setValue (v);
        else
            throw std::runtime_error ("Wrong field type");
    }

    /* ------------- NEW SCHOOL TYPED FIELDS SUBSCRIPT OPERATOR ------------- */

/*    void operator() (SFieldU32 const& f, std::uint32_t v)
    {
        setFieldU32(f, v);
    }
*/

    void set (SFieldU8 const& f, unsigned char v)
    {
        setFieldU8(f, v);
    }
    void set (SFieldU16 const& f, std::uint16_t v)
    {
        setFieldU16(f, v);
    }
    void set (SFieldU32 const& f, std::uint32_t v)
    {
        setFieldU32(f, v);
    }
    void set (SFieldU64 const& f, std::uint64_t v)
    {
        setFieldU64(f, v);
    }
    void set (SFieldH128 const& f, uint128 const& v)
    {
        setFieldH128(f, v);
    }
    void set (SFieldH256 const& f, uint256 const& v)
    {
        setFieldH256(f, v);
    }
    void set (SFieldVL const& f, Blob const& v)
    {
        setFieldVL(f, v);
    }
    void set (SFieldAccount const& f, Account const& v)
    {
        setFieldAccount(f, v);
    }
    void set (SFieldAccount const& f, RippleAddress const& v)
    {
        setFieldAccount(f, v);
    }
    void set (SFieldAmount const& f, STAmount const& v)
    {
        setFieldAmount(f, v);
    }
    void set (SFieldPathSet const& f, STPathSet const& v)
    {
        setFieldPathSet(f, v);
    }
    void set (SFieldV256 const& f, STVector256 const& v)
    {
        setFieldV256(f, v);
    }
    void set (SFieldArray const& f, STArray const& v)
    {
        setFieldArray(f, v);
    }

    template <class Tag>
    void set (SFieldH160 const& f, base_uint<160, Tag> const& v)
    {
        setFieldH160(f, v);
    }

    /* Primarily for writing terse tests  */
    unsigned char operator[] (SFieldU8 const& /*SField::ref*/ field) const
    {
        return getFieldU8(field);
    }
    std::uint16_t operator[] (SFieldU16 const& /*SField::ref*/ field) const
    {
        return getFieldU16(field);
    }
    std::uint32_t operator[] (SFieldU32 const& /*SField::ref*/ field) const
    {
        return getFieldU32(field);
    }
    std::uint64_t operator[] (SFieldU64 const& /*SField::ref*/ field) const
    {
        return getFieldU64(field);
    }
    uint128 operator[] (SFieldH128 const& /*SField::ref*/ field) const
    {
        return getFieldH128(field);
    }
    uint160 operator[] (SFieldH160 const& /*SField::ref*/ field) const
    {
        return getFieldH160(field);
    }
    uint256 operator[] (SFieldH256 const& /*SField::ref*/ field) const
    {
        return getFieldH256(field);
    }
/*    RippleAddress operator[] (SFieldAccount const& SField::ref field) const
    {
        return getFieldAccount(field);
    }
*/
    Account operator[] (SFieldAccount const& /*SField::ref*/ field) const
    {
        return getFieldAccount160(field);
    }
    Blob operator[] (SFieldVL const& /*SField::ref*/ field) const
    {
        return getFieldVL(field);
    }
    STAmount const& operator[] (SFieldAmount const& /*SField::ref*/ field) const
    {
        return getFieldAmount(field);
    }
    STPathSet const& operator[] (SFieldPathSet const& /*SField::ref*/ field) const
    {
        return getFieldPathSet(field);
    }
    const STVector256& operator[] (SFieldV256 const& /*SField::ref*/ field) const
    {
        return getFieldV256(field);
    }
    const STArray& operator[] (SFieldArray const& /*SField::ref*/  field) const
    {
        return getFieldArray(field);
    }
    /* ---------------------------------------------------------------------- */

    STObject& peekFieldObject (SField::ref field);

    bool isFieldPresent (SField::ref field) const;
    STBase* makeFieldPresent (SField::ref field);
    void makeFieldAbsent (SField::ref field);
    bool delField (SField::ref field);
    void delField (int index);

    static std::unique_ptr <STBase>
    makeDefaultObject (SerializedTypeID id, SField::ref name);

    // VFALCO TODO remove the 'depth' parameter
    static std::unique_ptr<STBase> makeDeserializedObject (
        SerializedTypeID id,
        SField::ref name,
        SerialIter&,
        int depth);

    static std::unique_ptr<STBase>
    makeNonPresentObject (SField::ref name)
    {
        return makeDefaultObject (STI_NOTPRESENT, name);
    }

    static std::unique_ptr<STBase> makeDefaultObject (SField::ref name)
    {
        return makeDefaultObject (name.fieldType, name);
    }

    // field iterator stuff
    typedef boost::ptr_vector<STBase>::iterator iterator;
    typedef boost::ptr_vector<STBase>::const_iterator const_iterator;
    iterator begin ()
    {
        return mData.begin ();
    }
    iterator end ()
    {
        return mData.end ();
    }
    const_iterator begin () const
    {
        return mData.begin ();
    }
    const_iterator end () const
    {
        return mData.end ();
    }
    bool empty () const
    {
        return mData.empty ();
    }

    bool hasMatchingEntry (const STBase&);

    bool operator== (const STObject & o) const;
    bool operator!= (const STObject & o) const
    {
        return ! (*this == o);
    }

    std::unique_ptr<STBase>
    duplicate () const override
    {
        return std::make_unique<STObject>(*this);
    }

private:
    // Implementation for getting (most) fields that return by value.
    //
    // The remove_cv and remove_reference are necessitated by the STBitString
    // types.  Their getValue returns by const ref.  We return those types
    // by value.
    template <typename T, typename V =
        typename std::remove_cv < typename std::remove_reference <
            decltype (std::declval <T> ().getValue ())>::type >::type >
    V getFieldByValue (SField::ref field) const
    {
        const STBase* rf = peekAtPField (field);

        if (!rf)
            throw std::runtime_error ("Field not found");

        SerializedTypeID id = rf->getSType ();

        if (id == STI_NOTPRESENT)
            return V (); // optional field not present

        const T* cf = dynamic_cast<const T*> (rf);

        if (!cf)
            throw std::runtime_error ("Wrong field type");

        return cf->getValue ();
    }

    // Implementations for getting (most) fields that return by const reference.
    //
    // If an absent optional field is deserialized we don't have anything
    // obvious to return.  So we insist on having the call provide an
    // 'empty' value we return in that circumstance.
    template <typename T, typename V>
    V const& getFieldByConstRef (SField::ref field, V const& empty) const
    {
        const STBase* rf = peekAtPField (field);

        if (!rf)
            throw std::runtime_error ("Field not found");

        SerializedTypeID id = rf->getSType ();

        if (id == STI_NOTPRESENT)
            return empty; // optional field not present

        const T* cf = dynamic_cast<const T*> (rf);

        if (!cf)
            throw std::runtime_error ("Wrong field type");

        return *cf;
    }

    // Implementation for setting most fields with a setValue() method.
    template <typename T, typename V>
    void setFieldUsingSetValue (SField::ref field, V value)
    {
        STBase* rf = getPField (field, true);

        if (!rf)
            throw std::runtime_error ("Field not found");

        if (rf->getSType () == STI_NOTPRESENT)
            rf = makeFieldPresent (field);

        T* cf = dynamic_cast<T*> (rf);

        if (!cf)
            throw std::runtime_error ("Wrong field type");

        cf->setValue (value);
    }

    // Implementation for setting fields using assignment
    template <typename T>
    void setFieldUsingAssignment (SField::ref field, T const& value)
    {
        STBase* rf = getPField (field, true);

        if (!rf)
            throw std::runtime_error ("Field not found");

        if (rf->getSType () == STI_NOTPRESENT)
            rf = makeFieldPresent (field);

        T* cf = dynamic_cast<T*> (rf);

        if (!cf)
            throw std::runtime_error ("Wrong field type");

        (*cf) = value;
    }

private:
    boost::ptr_vector<STBase>   mData;
    const SOTemplate*                   mType;
};

} // ripple

#endif
