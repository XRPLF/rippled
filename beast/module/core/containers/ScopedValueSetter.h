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

#ifndef BEAST_SCOPEDVALUESETTER_H_INCLUDED
#define BEAST_SCOPEDVALUESETTER_H_INCLUDED

namespace beast
{

//==============================================================================
/**
    Helper class providing an RAII-based mechanism for temporarily setting and
    then re-setting a value.

    E.g. @code
    int x = 1;

    {
        ScopedValueSetter setter (x, 2);

        // x is now 2
    }

    // x is now 1 again

    {
        ScopedValueSetter setter (x, 3, 4);

        // x is now 3
    }

    // x is now 4
    @endcode

*/
template <typename ValueType>
class ScopedValueSetter : public Uncopyable
{
public:
    /** Creates a ScopedValueSetter that will immediately change the specified value to the
        given new value, and will then reset it to its original value when this object is deleted.
    */
    ScopedValueSetter (ValueType& valueToSet,
                       ValueType newValue)
        : value (valueToSet),
          originalValue (valueToSet)
    {
        valueToSet = newValue;
    }

    /** Creates a ScopedValueSetter that will immediately change the specified value to the
        given new value, and will then reset it to be valueWhenDeleted when this object is deleted.
    */
    ScopedValueSetter (ValueType& valueToSet,
                       ValueType newValue,
                       ValueType valueWhenDeleted)
        : value (valueToSet),
          originalValue (valueWhenDeleted)
    {
        valueToSet = newValue;
    }

    ~ScopedValueSetter()
    {
        value = originalValue;
    }

private:
    //==============================================================================
    ValueType& value;
    const ValueType originalValue;
};

} // beast

#endif   // BEAST_SCOPEDVALUESETTER_H_INCLUDED
