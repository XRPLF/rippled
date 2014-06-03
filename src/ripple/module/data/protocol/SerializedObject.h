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

#ifndef RIPPLE_SERIALIZEDOBJECT_H
#define RIPPLE_SERIALIZEDOBJECT_H

#include <boost/ptr_container/ptr_vector.hpp> // VFALCO NOTE this looks like junk

namespace ripple {

class STArray;

class STObject
    : public SerializedType
    , public CountedObject <STObject>
{
public:
    static char const* getCountedObjectName () { return "STObject"; }

    STObject () : mType (nullptr)
    {
        ;
    }

    explicit STObject (SField::ref name) : SerializedType (name), mType (nullptr)
    {
        ;
    }

    STObject (const SOTemplate & type, SField::ref name) : SerializedType (name)
    {
        set (type);
    }

    STObject (const SOTemplate & type, SerializerIterator & sit, SField::ref name) : SerializedType (name)
    {
        set (sit);
        setType (type);
    }

    STObject (SField::ref name, boost::ptr_vector<SerializedType>& data) : SerializedType (name), mType (nullptr)
    {
        mData.swap (data);
    }

    std::unique_ptr <STObject> oClone () const
    {
        return std::unique_ptr<STObject> (new STObject (*this));
    }

    virtual ~STObject () { }

    static std::unique_ptr<SerializedType> deserialize (SerializerIterator & sit, SField::ref name);

    bool setType (const SOTemplate & type);
    bool isValidForType ();
    bool isFieldAllowed (SField::ref);
    bool isFree () const
    {
        return mType == nullptr;
    }

    void set (const SOTemplate&);
    bool set (SerializerIterator & u, int depth = 0);

    virtual SerializedTypeID getSType () const
    {
        return STI_OBJECT;
    }
    virtual bool isEquivalent (const SerializedType & t) const;
    virtual bool isDefault () const
    {
        return mData.empty ();
    }

    virtual void add (Serializer & s) const
    {
        add (s, true);    // just inner elements
    }

    void add (Serializer & s, bool withSignature) const;

    // VFALCO NOTE does this return an expensive copy of an object with a dynamic buffer?
    // VFALCO TODO Remove this function and fix the few callers.
    Serializer getSerializer () const
    {
        Serializer s;
        add (s);
        return s;
    }

    std::string getFullText () const;
    std::string getText () const;
    virtual Json::Value getJson (int options) const;

    int addObject (const SerializedType & t)
    {
        mData.push_back (t.clone ().release ());
        return mData.size () - 1;
    }
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

    // these throw if the field type doesn't match, or return default values if the
    // field is optional but not present
    std::string getFieldString (SField::ref field) const;
    unsigned char getFieldU8 (SField::ref field) const;
    std::uint16_t getFieldU16 (SField::ref field) const;
    std::uint32_t getFieldU32 (SField::ref field) const;
    std::uint64_t getFieldU64 (SField::ref field) const;
    uint128 getFieldH128 (SField::ref field) const;
    uint160 getFieldH160 (SField::ref field) const;
    uint256 getFieldH256 (SField::ref field) const;
    RippleAddress getFieldAccount (SField::ref field) const;
    uint160 getFieldAccount160 (SField::ref field) const;
    Blob getFieldVL (SField::ref field) const;
    const STAmount& getFieldAmount (SField::ref field) const;
    const STPathSet& getFieldPathSet (SField::ref field) const;
    const STVector256& getFieldV256 (SField::ref field) const;
    const STArray& getFieldArray (SField::ref field) const;

    void setFieldU8 (SField::ref field, unsigned char);
    void setFieldU16 (SField::ref field, std::uint16_t);
    void setFieldU32 (SField::ref field, std::uint32_t);
    void setFieldU64 (SField::ref field, std::uint64_t);
    void setFieldH128 (SField::ref field, const uint128&);
    void setFieldH160 (SField::ref field, const uint160&);
    void setFieldH256 (SField::ref field, uint256 const& );
    void setFieldVL (SField::ref field, Blob const&);
    void setFieldAccount (SField::ref field, const uint160&);
    void setFieldAccount (SField::ref field, const RippleAddress & addr)
    {
        setFieldAccount (field, addr.getAccountID ());
    }
    void setFieldAmount (SField::ref field, const STAmount&);
    void setFieldPathSet (SField::ref field, const STPathSet&);
    void setFieldV256 (SField::ref field, const STVector256 & v);

    STObject& peekFieldObject (SField::ref field);

    bool isFieldPresent (SField::ref field) const;
    SerializedType* makeFieldPresent (SField::ref field);
    void makeFieldAbsent (SField::ref field);
    bool delField (SField::ref field);
    void delField (int index);

    static std::unique_ptr <SerializedType> makeDefaultObject (SerializedTypeID id, SField::ref name);

    // VFALCO TODO remove the 'depth' parameter
    static std::unique_ptr<SerializedType> makeDeserializedObject (
        SerializedTypeID id,
        SField::ref name,
        SerializerIterator&,
        int depth);

    static std::unique_ptr<SerializedType> makeNonPresentObject (SField::ref name)
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
    /** Returns a value or throws if out of range.

        This will throw if the source value cannot be represented
        within the destination type.
    */
    // VFALCO NOTE This won't work right
    /*
    template <class T, class U>
    static T getWithRangeCheck (U v)
    {
        if (v < std::numeric_limits <T>::min ()) ||
            v > std::numeric_limits <T>::max ())
        {
            throw std::runtime_error ("Value out of range");
        }

        return static_cast <T> (v);
    }
    */

    STObject* duplicate () const
    {
        return new STObject (*this);
    }

private:
    boost::ptr_vector<SerializedType>   mData;
    const SOTemplate*                   mType;
};

//------------------------------------------------------------------------------

// VFALCO TODO these parameters should not be const references.
template <typename T, typename U>
static T range_check_cast (const U& value, const T& minimum, const T& maximum)
{
    if ((value < minimum) || (value > maximum))
        throw std::runtime_error ("Value out of range");

    return static_cast<T> (value);
}

inline STObject::iterator range_begin (STObject& x)
{
    return x.begin ();
}
inline STObject::iterator range_end (STObject& x)
{
    return x.end ();
}

//------------------------------------------------------------------------------

class STArray
    : public SerializedType
    , public CountedObject <STArray>
{
public:
    static char const* getCountedObjectName () { return "STArray"; }

    typedef boost::ptr_vector<STObject>                         vector;
    typedef boost::ptr_vector<STObject>::iterator               iterator;
    typedef boost::ptr_vector<STObject>::const_iterator         const_iterator;
    typedef boost::ptr_vector<STObject>::reverse_iterator       reverse_iterator;
    typedef boost::ptr_vector<STObject>::const_reverse_iterator const_reverse_iterator;
    typedef boost::ptr_vector<STObject>::size_type              size_type;

public:
    STArray ()
    {
        ;
    }
    explicit STArray (int n)
    {
        value.reserve (n);
    }
    explicit STArray (SField::ref f) : SerializedType (f)
    {
        ;
    }
    STArray (SField::ref f, int n) : SerializedType (f)
    {
        value.reserve (n);
    }
    STArray (SField::ref f, const vector & v) : SerializedType (f), value (v)
    {
        ;
    }
    explicit STArray (vector & v) : value (v)
    {
        ;
    }

    static std::unique_ptr<SerializedType> deserialize (SerializerIterator & sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    const vector& getValue () const
    {
        return value;
    }
    vector& getValue ()
    {
        return value;
    }

    // VFALCO NOTE as long as we're married to boost why not use boost::iterator_facade?
    //
    // vector-like functions
    void push_back (const STObject & object)
    {
        value.push_back (object.oClone ().release ());
    }
    STObject& operator[] (int j)
    {
        return value[j];
    }
    const STObject& operator[] (int j) const
    {
        return value[j];
    }
    iterator begin ()
    {
        return value.begin ();
    }
    const_iterator begin () const
    {
        return value.begin ();
    }
    iterator end ()
    {
        return value.end ();
    }
    const_iterator end () const
    {
        return value.end ();
    }
    size_type size () const
    {
        return value.size ();
    }
    reverse_iterator rbegin ()
    {
        return value.rbegin ();
    }
    const_reverse_iterator rbegin () const
    {
        return value.rbegin ();
    }
    reverse_iterator rend ()
    {
        return value.rend ();
    }
    const_reverse_iterator rend () const
    {
        return value.rend ();
    }
    iterator erase (iterator pos)
    {
        return value.erase (pos);
    }
    STObject& front ()
    {
        return value.front ();
    }
    const STObject& front () const
    {
        return value.front ();
    }
    STObject& back ()
    {
        return value.back ();
    }
    const STObject& back () const
    {
        return value.back ();
    }
    void pop_back ()
    {
        value.pop_back ();
    }
    bool empty () const
    {
        return value.empty ();
    }
    void clear ()
    {
        value.clear ();
    }
    void swap (STArray & a)
    {
        value.swap (a.value);
    }

    virtual std::string getFullText () const;
    virtual std::string getText () const;
    virtual Json::Value getJson (int) const;
    virtual void add (Serializer & s) const;

    void sort (bool (*compare) (const STObject & o1, const STObject & o2));

    bool operator== (const STArray & s)
    {
        return value == s.value;
    }
    bool operator!= (const STArray & s)
    {
        return value != s.value;
    }

    virtual SerializedTypeID getSType () const
    {
        return STI_ARRAY;
    }
    virtual bool isEquivalent (const SerializedType & t) const;
    virtual bool isDefault () const
    {
        return value.empty ();
    }

private:
    vector value;

    STArray* duplicate () const
    {
        return new STArray (*this);
    }
    static STArray* construct (SerializerIterator&, SField::ref);
};

inline STArray::iterator range_begin (STArray& x)
{
    return x.begin ();
}
inline STArray::iterator range_end (STArray& x)
{
    return x.end ();
}

} // ripple

#endif
