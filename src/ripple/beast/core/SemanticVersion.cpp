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

#include <ripple/beast/core/SemanticVersion.h>
#include <ripple/beast/core/LexicalCast.h>

#include <algorithm>
#include <cassert>
#include <locale>

namespace beast {

std::string print_identifiers (SemanticVersion::identifier_list const& list)
{
    std::string ret;

    for (auto const& x : list)
    {
        if (!ret.empty ())
            ret += ".";
        ret += x;
    }

    return ret;
}

bool isNumeric (std::string const& s)
{
    int n;

    // Must be convertible to an integer
    if (!lexicalCastChecked (n, s))
        return false;

    // Must not have leading zeroes
    return std::to_string (n) == s;
}

bool chop (std::string const& what, std::string& input)
{
    auto ret = input.find (what);

    if (ret != 0)
        return false;

    input.erase (0, what.size ());
    return true;
}

bool chopUInt (int& value, int limit, std::string& input)
{
    // Must not be empty
    if (input.empty ())
        return false;

    auto left_iter = std::find_if_not (input.begin (), input.end (),
        [](std::string::value_type c)
        {
            return std::isdigit (c, std::locale::classic());
        });

    std::string item (input.begin (), left_iter);

    // Must not be empty
    if (item.empty ())
        return false;

    int n;

    // Must be convertible to an integer
    if (!lexicalCastChecked (n, item))
        return false;

    // Must not have leading zeroes
    if (std::to_string (n) != item)
        return false;

    // Must not be out of range
    if (n < 0 || n > limit)
        return false;

    input.erase (input.begin (), left_iter);
    value = n;

    return true;
}

bool extract_identifier (
    std::string& value, bool allowLeadingZeroes, std::string& input)
{
    // Must not be empty
    if (input.empty ())
        return false;

    // Must not have a leading 0
    if (!allowLeadingZeroes && input [0] == '0')
        return false;

    auto last = input.find_first_not_of (
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-");

    // Must not be empty
    if (last == 0)
        return false;

    value = input.substr (0, last);
    input.erase (0, last);
    return true;
}

bool extract_identifiers (
    SemanticVersion::identifier_list& identifiers,
    bool allowLeadingZeroes,
    std::string& input)
{
    if (input.empty ())
        return false;

    do {
        std::string s;

        if (!extract_identifier (s, allowLeadingZeroes, input))
            return false;
        identifiers.push_back (s);
    } while (chop (".", input));

    return true;
}

//------------------------------------------------------------------------------

SemanticVersion::SemanticVersion ()
    : majorVersion (0)
    , minorVersion (0)
    , patchVersion (0)
{
}

SemanticVersion::SemanticVersion (std::string const& version)
    : SemanticVersion ()
{
    if (!parse (version))
        throw std::invalid_argument ("invalid version string");
}

bool SemanticVersion::parse (std::string const& input)
{
    // May not have leading or trailing whitespace
    auto left_iter = std::find_if_not (input.begin (), input.end (),
        [](std::string::value_type c)
        {
            return std::isspace (c, std::locale::classic());
        });

    auto right_iter = std::find_if_not (input.rbegin (), input.rend (),
        [](std::string::value_type c)
        {
            return std::isspace (c, std::locale::classic());
        }).base ();

    // Must not be empty!
    if (left_iter >= right_iter)
        return false;

    std::string version (left_iter, right_iter);

    // May not have leading or trailing whitespace
    if (version != input)
        return false;

    // Must have major version number
    if (! chopUInt (majorVersion, std::numeric_limits <int>::max (), version))
        return false;
    if (! chop (".", version))
        return false;

    // Must have minor version number
    if (! chopUInt (minorVersion, std::numeric_limits <int>::max (), version))
        return false;
    if (! chop (".", version))
        return false;

    // Must have patch version number
    if (! chopUInt (patchVersion, std::numeric_limits <int>::max (), version))
        return false;

    // May have pre-release identifier list
    if (chop ("-", version))
    {
        if (!extract_identifiers (preReleaseIdentifiers, false, version))
            return false;

        // Must not be empty
        if (preReleaseIdentifiers.empty ())
            return false;
    }

    // May have metadata identifier list
    if (chop ("+", version))
    {
        if (!extract_identifiers (metaData, true, version))
            return false;

        // Must not be empty
        if (metaData.empty ())
            return false;
    }

    return version.empty ();
}

std::string SemanticVersion::print () const
{
    std::string s;

    s = std::to_string (majorVersion) + "." +
        std::to_string (minorVersion) + "." +
        std::to_string (patchVersion);

    if (!preReleaseIdentifiers.empty ())
    {
        s += "-";
        s += print_identifiers (preReleaseIdentifiers);
    }

    if (!metaData.empty ())
    {
        s += "+";
        s += print_identifiers (metaData);
    }

    return s;
}

int compare (SemanticVersion const& lhs, SemanticVersion const& rhs)
{
    if (lhs.majorVersion > rhs.majorVersion)
        return 1;
    else if (lhs.majorVersion < rhs.majorVersion)
        return -1;

    if (lhs.minorVersion > rhs.minorVersion)
        return 1;
    else if (lhs.minorVersion < rhs.minorVersion)
        return -1;

    if (lhs.patchVersion > rhs.patchVersion)
        return 1;
    else if (lhs.patchVersion < rhs.patchVersion)
        return -1;

    if (lhs.isPreRelease () || rhs.isPreRelease ())
    {
        // Pre-releases have a lower precedence
        if (lhs.isRelease () && rhs.isPreRelease ())
            return 1;
        else if (lhs.isPreRelease () && rhs.isRelease ())
            return -1;

        // Compare pre-release identifiers
        for (int i = 0; i < std::max (lhs.preReleaseIdentifiers.size (), rhs.preReleaseIdentifiers.size ()); ++i)
        {
            // A larger list of identifiers has a higher precedence
            if (i >= rhs.preReleaseIdentifiers.size ())
                return 1;
            else if (i >= lhs.preReleaseIdentifiers.size ())
                return -1;

            std::string const& left (lhs.preReleaseIdentifiers [i]);
            std::string const& right (rhs.preReleaseIdentifiers [i]);

            // Numeric identifiers have lower precedence
            if (! isNumeric (left) && isNumeric (right))
                return 1;
            else if (isNumeric (left) && ! isNumeric (right))
                return -1;

            if (isNumeric (left))
            {
                assert(isNumeric (right));

                int const iLeft (lexicalCastThrow <int> (left));
                int const iRight (lexicalCastThrow <int> (right));

                if (iLeft > iRight)
                    return 1;
                else if (iLeft < iRight)
                    return -1;
            }
            else
            {
                assert (! isNumeric (right));

                int result = left.compare (right);

                if (result != 0)
                    return result;
            }
        }
    }

    // metadata is ignored

    return 0;
}

} // beast
