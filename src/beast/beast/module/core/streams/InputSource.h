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

#ifndef BEAST_INPUTSOURCE_H_INCLUDED
#define BEAST_INPUTSOURCE_H_INCLUDED

namespace beast
{

//==============================================================================
/**
    A lightweight object that can create a stream to read some kind of resource.

    This may be used to refer to a file, or some other kind of source, allowing a
    caller to create an input stream that can read from it when required.

    @see FileInputSource
*/
class InputSource : LeakChecked <InputSource>
{
public:
    //==============================================================================
    InputSource() noexcept      {}

    /** Destructor. */
    virtual ~InputSource()      {}

    //==============================================================================
    /** Returns a new InputStream to read this item.

        @returns            an inputstream that the caller will delete, or nullptr if
                            the filename isn't found.
    */
    virtual InputStream* createInputStream() = 0;

    /** Returns a new InputStream to read an item, relative.

        @param relatedItemPath  the relative pathname of the resource that is required
        @returns            an inputstream that the caller will delete, or nullptr if
                            the item isn't found.
    */
    virtual InputStream* createInputStreamFor (const String& relatedItemPath) = 0;

    /** Returns a hash code that uniquely represents this item.
    */
    virtual std::int64_t hashCode() const = 0;
};

} // beast

#endif   // BEAST_INPUTSOURCE_H_INCLUDED
