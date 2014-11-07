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

#include <ripple/basics/Log.h>
#include <ripple/protocol/SerializedType.h>
#include <ripple/protocol/STArray.h>

namespace ripple {

std::unique_ptr<SerializedType>
STArray::deserialize (SerializerIterator& sit, SField::ref field)
{
    std::unique_ptr <STArray> ret (std::make_unique <STArray> (field));
    vector& value (ret->getValue ());

    while (!sit.empty ())
    {
        int type, field;
        sit.getFieldID (type, field);

        if ((type == STI_ARRAY) && (field == 1))
            break;

        if ((type == STI_OBJECT) && (field == 1))
        {
            WriteLog (lsWARNING, STObject) <<
                "Encountered array with end of object marker";
            throw std::runtime_error ("Illegal terminator in array");
        }

        SField::ref fn = SField::getField (type, field);

        if (fn.isInvalid ())
        {
            WriteLog (lsTRACE, STObject) <<
                "Unknown field: " << type << "/" << field;
            throw std::runtime_error ("Unknown field");
        }

        if (fn.fieldType != STI_OBJECT)
        {
            WriteLog (lsTRACE, STObject) << "Array contains non-object";
            throw std::runtime_error ("Non-object in array");
        }

        value.push_back (new STObject (fn));
        value.rbegin ()->set (sit, 1);
    }
    return std::move (ret);
}

std::string STArray::getFullText () const
{
    std::string r = "[";

    bool first = true;
    for (STObject const& o : value)
    {
        if (!first)
            r += ",";

        r += o.getFullText ();
        first = false;
    }

    r += "]";
    return r;
}

std::string STArray::getText () const
{
    std::string r = "[";

    bool first = true;
    for (STObject const& o : value)
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
    for (auto const& object: value)
    {
        if (object.getSType () != STI_NOTPRESENT)
        {
            Json::Value inner = Json::objectValue;
            auto const& fname = object.getFName ();
            auto k = fname.hasName () ? fname.fieldName : std::to_string(index);
            inner[k] = object.getJson (p);
            v.append (inner);
            index++;
        }
    }
    return v;
}

void STArray::add (Serializer& s) const
{
    for (STObject const& object : value)
    {
        object.addFieldID (s);
        object.add (s);
        s.addFieldID (STI_OBJECT, 1);
    }
}

bool STArray::isEquivalent (const SerializedType& t) const
{
    const STArray* v = dynamic_cast<const STArray*> (&t);

    if (!v)
    {
        WriteLog (lsDEBUG, STObject) <<
            "notEquiv " << getFullText() << " not array";
        return false;
    }

    return value == v->value;
}

void STArray::sort (bool (*compare) (const STObject&, const STObject&))
{
    value.sort (compare);
}

} // ripple
