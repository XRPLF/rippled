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
#include <ripple/protocol/SerializedType.h>
#include <ripple/protocol/SerializedTypes.h>
#include <ripple/protocol/SerializedObjectTemplate.h>
#include <boost/ptr_container/ptr_vector.hpp>

namespace ripple {

class STArray;

class STObject
    : public SerializedType
    , public CountedObject <STObject>
{
private:
    enum class IncludeSigningFields : unsigned char
    {
        no,
        yes,
    };

public:
    static char const* getCountedObjectName () { return "STObject"; }

    STObject () : mType (nullptr)
    {
        ;
    }

    explicit STObject (SField::ref name)
        : SerializedType (name), mType (nullptr)
    {
        ;
    }

    STObject (const SOTemplate & type, SField::ref name)
        : SerializedType (name)
    {
        set (type);
    }

    STObject (
        const SOTemplate & type, SerializerIterator & sit, SField::ref name)
        : SerializedType (name)
    {
        set (sit);
        setType (type);
    }

    STObject (SField::ref name, boost::ptr_vector<SerializedType>& data)
        : SerializedType (name), mType (nullptr)
    {
        mData.swap (data);
    }

    std::unique_ptr <STObject> oClone () const
    {
        return std::make_unique <STObject> (*this);
    }

    virtual ~STObject () { }

    static std::unique_ptr<SerializedType>
    deserialize (SerializerIterator & sit, SField::ref name);

    bool setType (const SOTemplate & type);
    bool isValidForType ();
    bool isFieldAllowed (SField::ref);
    bool isFree () const
    {
        return mType == nullptr;
    }

    void set (const SOTemplate&);
    bool set (SerializerIterator & u, int depth = 0);

    virtual SerializedTypeID getSType () const override
    {
        return STI_OBJECT;
    }
    virtual bool isEquivalent (const SerializedType & t) const override;
    virtual bool isDefault () const override
    {
        return mData.empty ();
    }

    virtual void add (Serializer & s) const override
    {
        add (s, IncludeSigningFields::yes);    // just inner elements
    }

    // VFALCO NOTE does this return an expensive copy of an object with a
    //             dynamic buffer?
    // VFALCO TODO Remove this function and fix the few callers.
    Serializer getSerializer () const
    {
        Serializer s;
        add (s, IncludeSigningFields::yes);
        return s;
    }

    virtual std::string getFullText () const override;
    virtual std::string getText () const override;

    // TODO(tom): options should be an enum.
    virtual Json::Value getJson (int options) const override;

    int addObject (const SerializedType & t)
    {
        mData.push_back (t.clone ().release ());
        return mData.size () - 1;
    }
    const boost::ptr_vector<SerializedType>& peekData () const
    {
        return mData;
    }
    boost::ptr_vector<SerializedType>& peekData ()
    {
        return mData;
    }
    SerializedType& front ()
    {
        return mData.front ();
    }
    const SerializedType& front () const
    {
        return mData.front ();
    }
    SerializedType& back ()
    {
        return mData.back ();
    }
    const SerializedType& back () const
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

    const SerializedType& peekAtIndex (int offset) const
    {
        return mData[offset];
    }
    SerializedType& getIndex (int offset)
    {
        return mData[offset];
    }
    const SerializedType* peekAtPIndex (int offset) const
    {
        return & (mData[offset]);
    }
    SerializedType* getPIndex (int offset)
    {
        return & (mData[offset]);
    }

    int getFieldIndex (SField::ref field) const;
    SField::ref getFieldSType (int index) const;

    const SerializedType& peekAtField (SField::ref field) const;
    SerializedType& getField (SField::ref field);
    const SerializedType* peekAtPField (SField::ref field) const;
    SerializedType* getPField (SField::ref field, bool createOkay = false);

    // these throw if the field type doesn't match, or return default values
    // if the field is optional but not present
    std::string getFieldString (SField::ref field) const;
    unsigned char getFieldU8 (SField::ref field) const;
    std::uint16_t getFieldU16 (SField::ref field) const;
    std::uint32_t getFieldU32 (SField::ref field) const;
    std::uint64_t getFieldU64 (SField::ref field) const;
    uint128 getFieldH128 (SField::ref field) const;

    uint160 getFieldH160 (SField::ref field) const;
    uint256 getFieldH256 (SField::ref field) const;
    RippleAddress getFieldAccount (SField::ref field) const;
    Account getFieldAccount160 (SField::ref field) const;

    Blob getFieldVL (SField::ref field) const;
    STAmount const& getFieldAmount (SField::ref field) const;
    STPathSet const& getFieldPathSet (SField::ref field) const;
    const STVector256& getFieldV256 (SField::ref field) const;
    const STArray& getFieldArray (SField::ref field) const;
    const STObject& getFieldObject (SField::ref field) const;

    void setFieldU8 (SField::ref field, unsigned char);
    void setFieldU16 (SField::ref field, std::uint16_t);
    void setFieldU32 (SField::ref field, std::uint32_t);
    void setFieldU64 (SField::ref field, std::uint64_t);
    void setFieldH128 (SField::ref field, uint128 const&);
    void setFieldH256 (SField::ref field, uint256 const& );
    void setFieldVL (SField::ref field, Blob const&);
    void setFieldAccount (SField::ref field, Account const&);
    void setFieldAccount (SField::ref field, RippleAddress const& addr)
    {
        setFieldAccount (field, addr.getAccountID ());
    }
    void setFieldAmount (SField::ref field, STAmount const&);
    void setFieldPathSet (SField::ref field, STPathSet const&);
    void setFieldV256 (SField::ref field, STVector256 const& v);
    void setFieldArray (SField::ref field, STArray const& v);
    void setFieldObject (SField::ref field, STObject const& v);

    template <class Tag>
    void setFieldH160 (SField::ref field, base_uint<160, Tag> const& v)
    {
        SerializedType* rf = getPField (field, true);

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

    STObject& peekFieldObject (SField::ref field);

    bool isFieldPresent (SField::ref field) const;
    SerializedType* makeFieldPresent (SField::ref field);
    void makeFieldAbsent (SField::ref field);
    bool delField (SField::ref field);
    void delField (int index);

    static std::unique_ptr <SerializedType>
    makeDefaultObject (SerializedTypeID id, SField::ref name);

    // VFALCO TODO remove the 'depth' parameter
    static std::unique_ptr<SerializedType> makeDeserializedObject (
        SerializedTypeID id,
        SField::ref name,
        SerializerIterator&,
        int depth);

    static std::unique_ptr<SerializedType>
    makeNonPresentObject (SField::ref name)
    {
        return makeDefaultObject (STI_NOTPRESENT, name);
    }

    static std::unique_ptr<SerializedType> makeDefaultObject (SField::ref name)
    {
        return makeDefaultObject (name.fieldType, name);
    }

    // field iterator stuff
    typedef boost::ptr_vector<SerializedType>::iterator iterator;
    typedef boost::ptr_vector<SerializedType>::const_iterator const_iterator;
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

    bool hasMatchingEntry (const SerializedType&);

    bool operator== (const STObject & o) const;
    bool operator!= (const STObject & o) const
    {
        return ! (*this == o);
    }

private:
    int giveObject (std::unique_ptr<SerializedType> t)
    {
        mData.push_back (t.release ());
        return mData.size () - 1;
    }
    int giveObject (SerializedType * t)
    {
        mData.push_back (t);
        return mData.size () - 1;
    }

    void add (Serializer & s, IncludeSigningFields sortType) const;

    virtual STObject* duplicate () const override
    {
        return new STObject (*this);
    }


    // Types and functions for sorting the entries in an STObject in the
    // order that they will be serialized.  Note: they are not sorted into
    // pointer value order, they are sorted by
    using SortedFieldPtrVec = std::vector<SerializedType const*>;
    static SortedFieldPtrVec getSortedFields (
        STObject const& objToSort, IncludeSigningFields sortType);

    // Two different ways to compare STObjects.
    //
    // This one works only if the SOTemplates are the same.  Presumably it
    // runs faster since there's no sorting.
    static bool equivalentSTObjectSameTemplate (
        STObject const& obj1, STObject const& obj2);

    // This way of comparing STObjects always works, but is slower.
    static bool equivalentSTObject (STObject const& obj1, STObject const& obj2);

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
        const SerializedType* rf = peekAtPField (field);

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
        const SerializedType* rf = peekAtPField (field);

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
        SerializedType* rf = getPField (field, true);

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
        SerializedType* rf = getPField (field, true);

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
    boost::ptr_vector<SerializedType>   mData;
    const SOTemplate*                   mType;
};

} // ripple

#endif
