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

#ifndef RIPPLE_PROTOCOL_STBASE_H_INCLUDED
#define RIPPLE_PROTOCOL_STBASE_H_INCLUDED

#include <ripple/basics/contract.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/Serializer.h>
#include <ostream>
#include <memory>
#include <string>
#include <typeinfo>
#include <utility>
#include <type_traits>
namespace ripple {

enum class JsonOptions {
    none         = 0,
    include_date = 1
};

// VFALCO TODO fix this restriction on copy assignment.
//
// CAUTION: Do not create a vector (or similar container) of any object derived
// from STBase. Use Boost ptr_* containers. The copy assignment operator
// of STBase has semantics that will cause contained types to change
// their names when an object is deleted because copy assignment is used to
// "slide down" the remaining types and this will not copy the field
// name. Changing the copy assignment operator to copy the field name breaks the
// use of copy assignment just to copy values, which is used in the transaction
// engine code.

//------------------------------------------------------------------------------

/** A type which can be exported to a well known binary format.

    A STBase:
        - Always a field
        - Can always go inside an eligible enclosing STBase
            (such as STArray)
        - Has a field name

    Like JSON, a SerializedObject is a basket which has rules
    on what it can hold.

    @note "ST" stands for "Serialized Type."
*/
class STBase
{
public:
    STBase();

    explicit
    STBase (SField const& n);

    virtual ~STBase() = default;

    STBase(const STBase& t) = default;
    STBase& operator= (const STBase& t);

    bool operator== (const STBase& t) const;
    bool operator!= (const STBase& t) const;

    virtual
    STBase*
    copy (std::size_t n, void* buf) const
    {
        return emplace(n, buf, *this);
    }

    virtual
    STBase*
    move (std::size_t n, void* buf)
    {
        return emplace(n, buf, std::move(*this));
    }

    template <class D>
    D&
    downcast()
    {
        D* ptr = dynamic_cast<D*> (this);
        if (ptr == nullptr)
            Throw<std::bad_cast> ();
        return *ptr;
    }

    template <class D>
    D const&
    downcast() const
    {
        D const * ptr = dynamic_cast<D const*> (this);
        if (ptr == nullptr)
            Throw<std::bad_cast> ();
        return *ptr;
    }

    virtual
    SerializedTypeID
    getSType() const;

    virtual
    std::string
    getFullText() const;

    virtual
    std::string
    getText() const;

    virtual
    Json::Value
    getJson (JsonOptions /*options*/) const;

    virtual
    void
    add (Serializer& s) const;

    virtual
    bool
    isEquivalent (STBase const& t) const;

    virtual
    bool
    isDefault() const;

    /** A STBase is a field.
        This sets the name.
    */
    void
    setFName (SField const& n);

    SField const&
    getFName() const;

    void
    addFieldID (Serializer& s) const;

protected:
    SField const* fName;

    template <class T>
    static
    STBase*
    emplace(std::size_t n, void* buf, T&& val)
    {
        using U = std::decay_t<T>;
        if (sizeof(U) > n)
            return new U(std::forward<T>(val));
        return new(buf) U(std::forward<T>(val));
    }
};

//------------------------------------------------------------------------------

std::ostream& operator<< (std::ostream& out, const STBase& t);

} // ripple

#endif
