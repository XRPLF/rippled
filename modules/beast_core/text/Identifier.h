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

#ifndef BEAST_IDENTIFIER_H_INCLUDED
#define BEAST_IDENTIFIER_H_INCLUDED

namespace beast
{

class StringPool;


//==============================================================================
/**
    Represents a string identifier, designed for accessing properties by name.

    Identifier objects are very light and fast to copy, but slower to initialise
    from a string, so it's much faster to keep a static identifier object to refer
    to frequently-used names, rather than constructing them each time you need it.

    @see NamedPropertySet, ValueTree
*/
class BEAST_API Identifier
{
public:
    /** Creates a null identifier. */
    Identifier() noexcept;

    /** Creates an identifier with a specified name.
        Because this name may need to be used in contexts such as script variables or XML
        tags, it must only contain ascii letters and digits, or the underscore character.
    */
    Identifier (const char* name);

    /** Creates an identifier with a specified name.
        Because this name may need to be used in contexts such as script variables or XML
        tags, it must only contain ascii letters and digits, or the underscore character.
    */
    Identifier (const String& name);

    /** Creates a copy of another identifier. */
    Identifier (const Identifier& other) noexcept;

    /** Creates a copy of another identifier. */
    Identifier& operator= (const Identifier other) noexcept;

    /** Destructor */
    ~Identifier();

    /** Compares two identifiers. This is a very fast operation. */
    inline bool operator== (const Identifier other) const noexcept      { return name == other.name; }

    /** Compares two identifiers. This is a very fast operation. */
    inline bool operator!= (const Identifier other) const noexcept      { return name != other.name; }

    /** Returns this identifier as a string. */
    String toString() const                                             { return name; }

    /** Returns this identifier's raw string pointer. */
    operator const String::CharPointerType() const noexcept             { return name; }

    /** Returns this identifier's raw string pointer. */
    const String::CharPointerType getCharPointer() const noexcept       { return name; }

    /** Returns true if this Identifier is not null */
    bool isValid() const noexcept                                       { return name.getAddress() != nullptr; }

    /** Returns true if this Identifier is null */
    bool isNull() const noexcept                                        { return name.getAddress() == nullptr; }

    /** A null identifier. */
    static Identifier null;

    /** Checks a given string for characters that might not be valid in an Identifier.
        Since Identifiers are used as a script variables and XML attributes, they should only contain
        alphanumeric characters, underscores, or the '-' and ':' characters.
    */
    static bool isValidIdentifier (const String& possibleIdentifier) noexcept;


private:
    //==============================================================================
    String::CharPointerType name;

    static StringPool& getPool();
};

}  // namespace beast

#endif   // BEAST_IDENTIFIER_H_INCLUDED
