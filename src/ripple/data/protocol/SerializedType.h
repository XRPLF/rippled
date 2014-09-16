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

#ifndef RIPPLE_SERIALIZEDTYPE_H
#define RIPPLE_SERIALIZEDTYPE_H

#include <ripple/data/protocol/SField.h>
#include <ripple/data/protocol/Serializer.h>

namespace ripple {

// VFALCO TODO fix this restriction on copy assignment.
//
// CAUTION: Do not create a vector (or similar container) of any object derived
// from SerializedType. Use Boost ptr_* containers. The copy assignment operator
// of SerializedType has semantics that will cause contained types to change
// their names when an object is deleted because copy assignment is used to
// "slide down" the remaining types and this will not copy the field
// name. Changing the copy assignment operator to copy the field name breaks the
// use of copy assignment just to copy values, which is used in the transaction
// engine code.

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

    virtual ~SerializedType () = default;

    //
    // overridables
    //

    virtual
    SerializedTypeID
    getSType () const
    {
        return STI_NOTPRESENT;
    }

    virtual
    std::string
    getFullText() const;

    // just the value
    virtual
    std::string
    getText() const
    {
        return std::string();
    }
    
    virtual
    Json::Value getJson (int /*options*/) const
    {
        return getText();
    }

    virtual
    void
    add (Serializer& s) const
    {
        // VFALCO Why not just make this pure virtual?
        assert (false);
    }

    virtual
    bool
    isEquivalent (SerializedType const& t) const;

    virtual
    bool
    isDefault () const
    {
        return true;
    }

private:
    // VFALCO TODO Return std::unique_ptr <SerializedType>
    virtual
    SerializedType*
    duplicate () const
    {
        return new SerializedType (*fName);
    }

public:
    //
    // members
    //

    static
    std::unique_ptr <SerializedType>
    deserialize (SField::ref name)
    {
        return std::unique_ptr<SerializedType> (new SerializedType (name));
    }

    /** A SerializedType is a field.
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
    std::unique_ptr<SerializedType> clone () const
    {
        return std::unique_ptr<SerializedType> (duplicate ());
    }


    void addFieldID (Serializer& s) const
    {
        assert (fName->isBinary ());
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

} // ripple

#endif
