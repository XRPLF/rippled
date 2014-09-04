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

#ifndef RIPPLE_STARRAY_H
#define RIPPLE_STARRAY_H

#include "STObject.h"
#include <boost/ptr_container/ptr_vector.hpp>

namespace ripple {

class STArray final
    : public SerializedType
    , public CountedObject <STArray>
{
public:
    static char const* getCountedObjectName () { return "STArray"; }

    typedef boost::ptr_vector<STObject>    vector;

    typedef vector::iterator               iterator;
    typedef vector::const_iterator         const_iterator;
    typedef vector::reverse_iterator       reverse_iterator;
    typedef vector::const_reverse_iterator const_reverse_iterator;
    typedef vector::size_type              size_type;

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

    virtual ~STArray () { }

    static std::unique_ptr<SerializedType>
    deserialize (SerializerIterator & sit, SField::ref name);

    const vector& getValue () const
    {
        return value;
    }
    vector& getValue ()
    {
        return value;
    }

    // VFALCO NOTE as long as we're married to boost why not use
    //             boost::iterator_facade?
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

    virtual std::string getFullText () const override;
    virtual std::string getText () const override;

    virtual Json::Value getJson (int index) const override;
    virtual void add (Serializer & s) const override;

    void sort (bool (*compare) (const STObject & o1, const STObject & o2));

    bool operator== (const STArray & s)
    {
        return value == s.value;
    }
    bool operator!= (const STArray & s)
    {
        return value != s.value;
    }

    virtual SerializedTypeID getSType () const override
    {
        return STI_ARRAY;
    }
    virtual bool isEquivalent (const SerializedType & t) const override;
    virtual bool isDefault () const override
    {
        return value.empty ();
    }

private:
    virtual STArray* duplicate () const override
    {
        return new STArray (*this);
    }

private:
    vector value;
};

} // ripple

#endif
