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

#ifndef RIPPLE_SERIALIZEDOBJECTTEMPLATE_H
#define RIPPLE_SERIALIZEDOBJECTTEMPLATE_H

namespace ripple {

/** Flags for elements in a SerializedObjectTemplate.
*/
// VFALCO NOTE these don't look like bit-flags...
enum SOE_Flags
{
    SOE_INVALID  = -1,
    SOE_REQUIRED = 0,   // required
    SOE_OPTIONAL = 1,   // optional, may be present with default value
    SOE_DEFAULT  = 2,   // optional, if present, must not have default value
};

//------------------------------------------------------------------------------

/** An element in a SerializedObjectTemplate.
*/
class SOElement
{
public:
    SField::ref       e_field;
    SOE_Flags const   flags;

    SOElement (SField::ref fieldName, SOE_Flags flags)
        : e_field (fieldName)
        , flags (flags)
    {
    }
};

//------------------------------------------------------------------------------

/** Defines the fields and their attributes within a SerializedObject.
    Each subclass of SerializedObject will provide its own template
    describing the available fields and their metadata attributes.
*/
class SOTemplate
{
public:
    typedef std::unique_ptr <SOElement const> value_type;
    typedef std::vector <value_type> list_type;

    /** Create an empty template.
        After creating the template, call @ref push_back with the
        desired fields.
        @see push_back
    */
    SOTemplate ();

    // VFALCO NOTE Why do we even bother with the 'private' keyword if
    //             this function is present?
    //
    list_type const& peek () const
    {
        return mTypes;
    }

    /** Add an element to the template. */
    void push_back (SOElement const& r);

    /** Retrieve the position of a named field. */
    int getIndex (SField::ref) const;

private:
    list_type mTypes;

    std::vector <int> mIndex;       // field num -> index
};

} // ripple

#endif
