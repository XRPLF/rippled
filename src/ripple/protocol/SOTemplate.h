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

#ifndef RIPPLE_PROTOCOL_SOTEMPLATE_H_INCLUDED
#define RIPPLE_PROTOCOL_SOTEMPLATE_H_INCLUDED

#include <ripple/basics/contract.h>
#include <ripple/protocol/SField.h>
#include <boost/range.hpp>
#include <memory>

namespace ripple {

/** Flags for elements in a SOTemplate. */
// VFALCO NOTE these don't look like bit-flags...
enum SOE_Flags
{
    SOE_INVALID  = -1,
    SOE_REQUIRED = 0,   // required
    SOE_OPTIONAL = 1,   // optional, may be present with default value
    SOE_DEFAULT  = 2,   // optional, if present, must not have default value
};

//------------------------------------------------------------------------------

/** An element in a SOTemplate. */
class SOElement
{
public:
    SField const&     e_field;
    SOE_Flags const   flags;

    SOElement (SField const& fieldName, SOE_Flags flags)
        : e_field (fieldName)
        , flags (flags)
    {
        if (! e_field.isUseful())
            Throw<std::runtime_error> ("SField in SOElement must be useful.");
    }
};

//------------------------------------------------------------------------------

/** Defines the fields and their attributes within a STObject.
    Each subclass of SerializedObject will provide its own template
    describing the available fields and their metadata attributes.
*/
class SOTemplate
{
public:
    using list_type = std::vector <std::unique_ptr <SOElement const>>;
    using iterator_range = boost::iterator_range<list_type::const_iterator>;

    /** Create an empty template.
        After creating the template, call @ref push_back with the
        desired fields.
        @see push_back
    */
    SOTemplate () = default;

    SOTemplate(SOTemplate&& other)
        : mTypes(std::move(other.mTypes))
        , mIndex(std::move(other.mIndex))
    {
    }

    /* Provide for the enumeration of fields */
    iterator_range all () const
    {
        return boost::make_iterator_range(mTypes);
    }

    /** The number of entries in this template */
    std::size_t size () const
    {
        return mTypes.size ();
    }

    /** Add an element to the template. */
    void push_back (SOElement const& r);

    /** Retrieve the position of a named field. */
    int getIndex (SField const&) const;

    SOE_Flags
    style(SField const& sf) const
    {
        return mTypes[mIndex[sf.getNum()]]->flags;
    }

private:
    list_type mTypes;

    std::vector <int> mIndex;       // field num -> index
};

} // ripple

#endif
