//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_SEMANTICVERSION_H_INCLUDED
#define BEAST_SEMANTICVERSION_H_INCLUDED

#include <beast/strings/String.h>
#include <beast/module/core/text/StringArray.h>
#include <beast/utility/noexcept.h>

namespace beast {

/** A Semantic Version number.

    Identifies the build of a particular version of software using
    the Semantic Versioning Specification described here:

    http://semver.org/
*/
class SemanticVersion
{
public:
    int majorVersion;
    int minorVersion;
    int patchVersion;
    StringArray preReleaseIdentifiers;
    StringArray metaData;

    SemanticVersion ();

    /** Parse a semantic version string.
        The parsing is as strict as possible.
        @return `true` if the string was parsed.
    */
    bool parse (String input);

    /** Produce a string from semantic version components. */
    String print () const;

    inline bool isRelease () const noexcept { return preReleaseIdentifiers.size () <= 0; }
    inline bool isPreRelease () const noexcept { return ! isRelease (); }

    /** Compare this against another version.
        The comparison follows the rules as per the specification.
    */
    int compare (SemanticVersion const& rhs) const noexcept;

    inline bool operator== (SemanticVersion const& other) const noexcept { return compare (other) == 0; }
    inline bool operator!= (SemanticVersion const& other) const noexcept { return compare (other) != 0; }
    inline bool operator>= (SemanticVersion const& other) const noexcept { return compare (other) >= 0; }
    inline bool operator<= (SemanticVersion const& other) const noexcept { return compare (other) <= 0; }
    inline bool operator>  (SemanticVersion const& other) const noexcept { return compare (other) >  0; }
    inline bool operator<  (SemanticVersion const& other) const noexcept { return compare (other) <  0; }

private:
    static bool isNumeric (String const& s);
    static String printIdentifiers (StringArray const& list);
    static bool chop (String const& what, String& input);
    static bool chopUInt (int* value, int limit, String& input);
    static bool chopIdentifier (String* value, bool allowLeadingZeroes, String& input);
    static bool chopIdentifiers (StringArray* value, bool preRelease, String& input);
};

} // beast

#endif
