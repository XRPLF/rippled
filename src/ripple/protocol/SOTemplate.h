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
#include <functional>
#include <initializer_list>
#include <memory>
#include <stdexcept>

namespace ripple {

/** Kind of element in each entry of an SOTemplate. */
enum SOEStyle {
    soeINVALID = -1,
    soeREQUIRED = 0,  // required
    soeOPTIONAL = 1,  // optional, may be present with default value
    soeDEFAULT = 2,   // optional, if present, must not have default value
};

//------------------------------------------------------------------------------

/** An element in a SOTemplate. */
class SOElement
{
    // Use std::reference_wrapper so SOElement can be stored in a std::vector.
    std::reference_wrapper<SField const> sField_;
    SOEStyle style_;

public:
    SOElement(SField const& fieldName, SOEStyle style)
        : sField_(fieldName), style_(style)
    {
        if (!sField_.get().isUseful())
        {
            auto nm = std::to_string(fieldName.getCode());
            if (fieldName.hasName())
                nm += ": '" + fieldName.getName() + "'";
            Throw<std::runtime_error>(
                "SField (" + nm + ") in SOElement must be useful.");
        }
    }

    SField const&
    sField() const
    {
        return sField_.get();
    }

    SOEStyle
    style() const
    {
        return style_;
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
    // Copying vectors is expensive.  Make this a move-only type until
    // there is motivation to change that.
    SOTemplate(SOTemplate&& other) = default;
    SOTemplate&
    operator=(SOTemplate&& other) = default;

    /** Create a template populated with all fields.
        After creating the template fields cannot be
        added, modified, or removed.
    */
    SOTemplate(
        std::initializer_list<SOElement> uniqueFields,
        std::initializer_list<SOElement> commonFields = {});

    /* Provide for the enumeration of fields */
    std::vector<SOElement>::const_iterator
    begin() const
    {
        return elements_.cbegin();
    }

    std::vector<SOElement>::const_iterator
    cbegin() const
    {
        return begin();
    }

    std::vector<SOElement>::const_iterator
    end() const
    {
        return elements_.cend();
    }

    std::vector<SOElement>::const_iterator
    cend() const
    {
        return end();
    }

    /** The number of entries in this template */
    std::size_t
    size() const
    {
        return elements_.size();
    }

    /** Retrieve the position of a named field. */
    int
    getIndex(SField const&) const;

    SOEStyle
    style(SField const& sf) const
    {
        return elements_[indices_[sf.getNum()]].style();
    }

private:
    std::vector<SOElement> elements_;
    std::vector<int> indices_;  // field num -> index
};

}  // namespace ripple

#endif
