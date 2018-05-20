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

#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/STBase.h>
#include <ripple/protocol/STArray.h>

namespace ripple {

STArray::STArray()
{
    // VFALCO NOTE We need to determine if this is
    //             the right thing to do, and consider
    //             making it optional.
    //v_.reserve(reserveSize);
}

STArray::STArray (STArray&& other) noexcept
    : STBase(other.getFName())
    , v_(std::move(other.v_))
{
}

STArray& STArray::operator= (STArray&& other) noexcept
{
    setFName(other.getFName());
    v_ = std::move(other.v_);
    return *this;
}

STArray::STArray (int n)
{
    v_.reserve(n);
}

STArray::STArray (SField const& f)
    : STBase (f)
{
    v_.reserve(reserveSize);
}

STArray::STArray (SField const& f, int n)
    : STBase (f)
{
    v_.reserve(n);
}

STArray::STArray (SerialIter& sit, SField const& f, int depth)
    : STBase(f)
{
    while (!sit.empty ())
    {
        int type, field;
        sit.getFieldID (type, field);

        if ((type == STI_ARRAY) && (field == 1))
            break;

        if ((type == STI_OBJECT) && (field == 1))
        {
            JLOG (debugLog().error()) <<
                "Encountered array with end of object marker";
            Throw<std::runtime_error> ("Illegal terminator in array");
        }

        auto const& fn = SField::getField (type, field);

        if (fn.isInvalid ())
        {
            JLOG (debugLog().error()) <<
                "Unknown field: " << type << "/" << field;
            Throw<std::runtime_error> ("Unknown field");
        }

        if (fn.fieldType != STI_OBJECT)
        {
            JLOG (debugLog().error()) <<
                "Array contains non-object";
            Throw<std::runtime_error> ("Non-object in array");
        }

        v_.emplace_back(sit, fn, depth+1);

        if (v_.back().setTypeFromSField (fn) == STObject::typeSetFail)
        {
            Throw<std::runtime_error> ("Malformed object in array");
        }
    }
}

std::string STArray::getFullText () const
{
    std::string r = "[";

    bool first = true;
    for (auto const& obj : v_)
    {
        if (!first)
            r += ",";

        r += obj.getFullText ();
        first = false;
    }

    r += "]";
    return r;
}

std::string STArray::getText () const
{
    std::string r = "[";

    bool first = true;
    for (STObject const& o : v_)
    {
        if (!first)
            r += ",";

        r += o.getText ();
        first = false;
    }

    r += "]";
    return r;
}

Json::Value STArray::getJson (int p) const
{
    Json::Value v = Json::arrayValue;
    int index = 1;
    for (auto const& object: v_)
    {
        if (object.getSType () != STI_NOTPRESENT)
        {
            Json::Value& inner = v.append (Json::objectValue);
            auto const& fname = object.getFName ();
            auto k = fname.hasName () ? fname.fieldName : std::to_string(index);
            inner[k] = object.getJson (p);
            index++;
        }
    }
    return v;
}

void STArray::add (Serializer& s) const
{
    for (STObject const& object : v_)
    {
        object.addFieldID (s);
        object.add (s);
        s.addFieldID (STI_OBJECT, 1);
    }
}

bool STArray::isEquivalent (const STBase& t) const
{
    auto v = dynamic_cast<const STArray*> (&t);
    return v != nullptr && v_ == v->v_;
}

void STArray::sort (bool (*compare) (const STObject&, const STObject&))
{
    std::sort(v_.begin(), v_.end(), compare);
}

} // ripple
