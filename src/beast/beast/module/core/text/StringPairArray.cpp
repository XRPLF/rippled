//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

namespace beast
{

StringPairArray::StringPairArray (const bool ignoreCase_)
    : ignoreCase (ignoreCase_)
{
}

StringPairArray::StringPairArray (const StringPairArray& other)
    : keys (other.keys),
      values (other.values),
      ignoreCase (other.ignoreCase)
{
}

StringPairArray::~StringPairArray()
{
}

StringPairArray& StringPairArray::operator= (const StringPairArray& other)
{
    keys = other.keys;
    values = other.values;
    return *this;
}

void StringPairArray::swapWith (StringPairArray& other)
{
    std::swap (ignoreCase, other.ignoreCase);
    keys.swapWith (other.keys);
    values.swapWith (other.values);
}

bool StringPairArray::operator== (const StringPairArray& other) const
{
    for (int i = keys.size(); --i >= 0;)
        if (other [keys[i]] != values[i])
            return false;

    return true;
}

bool StringPairArray::operator!= (const StringPairArray& other) const
{
    return ! operator== (other);
}

const String& StringPairArray::operator[] (const String& key) const
{
    return values [keys.indexOf (key, ignoreCase)];
}

String StringPairArray::getValue (const String& key, const String& defaultReturnValue) const
{
    const int i = keys.indexOf (key, ignoreCase);

    if (i >= 0)
        return values[i];

    return defaultReturnValue;
}

void StringPairArray::set (const String& key, const String& value)
{
    const int i = keys.indexOf (key, ignoreCase);

    if (i >= 0)
    {
        values.set (i, value);
    }
    else
    {
        keys.add (key);
        values.add (value);
    }
}

void StringPairArray::addArray (const StringPairArray& other)
{
    for (int i = 0; i < other.size(); ++i)
        set (other.keys[i], other.values[i]);
}

void StringPairArray::clear()
{
    keys.clear();
    values.clear();
}

void StringPairArray::remove (const String& key)
{
    remove (keys.indexOf (key, ignoreCase));
}

void StringPairArray::remove (const int index)
{
    keys.remove (index);
    values.remove (index);
}

void StringPairArray::setIgnoresCase (const bool shouldIgnoreCase)
{
    ignoreCase = shouldIgnoreCase;
}

String StringPairArray::getDescription() const
{
    String s;

    for (int i = 0; i < keys.size(); ++i)
    {
        s << keys[i] << " = " << values[i];
        if (i < keys.size())
            s << ", ";
    }

    return s;
}

void StringPairArray::minimiseStorageOverheads()
{
    keys.minimiseStorageOverheads();
    values.minimiseStorageOverheads();
}

} // beast
