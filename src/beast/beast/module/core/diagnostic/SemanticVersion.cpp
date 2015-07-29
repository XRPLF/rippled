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

#include <beast/unit_test/suite.h>
#include <beast/module/core/text/LexicalCast.h>

#include <algorithm>

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

bool SemanticVersion::parse (std::string const& input, bool debug)
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
                bassert (isNumeric (right));

                int const iLeft (lexicalCastThrow <int> (left));
                int const iRight (lexicalCastThrow <int> (right));

                if (iLeft > iRight)
                    return 1;
                else if (iLeft < iRight)
                    return -1;
            }
            else
            {
                bassert (! isNumeric (right));

                int result = left.compare (right);

                if (result != 0)
                    return result;
            }
        }
    }

    // metadata is ignored

    return 0;
}

//------------------------------------------------------------------------------

class SemanticVersion_test: public unit_test::suite
{
    using identifier_list = SemanticVersion::identifier_list;

public:
    void checkPass (std::string const& input, bool shouldPass = true)
    {
        SemanticVersion v;

        if (shouldPass )
        {
            expect (v.parse (input));
            expect (v.print () == input);
        }
        else
        {
            expect (! v.parse (input));
        }
    }

    void checkFail (std::string const& input)
    {
        checkPass (input, false);
    }

    // check input and input with appended metadata
    void checkMeta (std::string const& input, bool shouldPass)
    {
        checkPass (input, shouldPass);

        checkPass (input + "+a", shouldPass);
        checkPass (input + "+1", shouldPass);
        checkPass (input + "+a.b", shouldPass);
        checkPass (input + "+ab.cd", shouldPass);

        checkFail (input + "!");
        checkFail (input + "+");
        checkFail (input + "++");
        checkFail (input + "+!");
        checkFail (input + "+.");
        checkFail (input + "+a.!");
    }

    void checkMetaFail (std::string const& input)
    {
        checkMeta (input, false);
    }

    // check input, input with appended release data,
    // input with appended metadata, and input with both
    // appended release data and appended metadata
    //
    void checkRelease (std::string const& input, bool shouldPass = true)
    {
        checkMeta (input, shouldPass);

        checkMeta (input + "-1", shouldPass);
        checkMeta (input + "-a", shouldPass);
        checkMeta (input + "-a1", shouldPass);
        checkMeta (input + "-a1.b1", shouldPass);
        checkMeta (input + "-ab.cd", shouldPass);
        checkMeta (input + "--", shouldPass);

        checkMetaFail (input + "+");
        checkMetaFail (input + "!");
        checkMetaFail (input + "-");
        checkMetaFail (input + "-!");
        checkMetaFail (input + "-.");
        checkMetaFail (input + "-a.!");
        checkMetaFail (input + "-0.a");
    }

    // Checks the major.minor.version string alone and with all
    // possible combinations of release identifiers and metadata.
    //
    void check (std::string const& input, bool shouldPass = true)
    {
        checkRelease (input, shouldPass);
    }

    void negcheck (std::string const& input)
    {
        check (input, false);
    }

    void testParse ()
    {
        testcase ("parsing");

        check ("0.0.0");
        check ("1.2.3");
        check ("2147483647.2147483647.2147483647"); // max int

        // negative values
        negcheck ("-1.2.3");
        negcheck ("1.-2.3");
        negcheck ("1.2.-3");

        // missing parts
        negcheck ("");
        negcheck ("1");
        negcheck ("1.");
        negcheck ("1.2");
        negcheck ("1.2.");
        negcheck (".2.3");

        // whitespace
        negcheck (" 1.2.3");
        negcheck ("1 .2.3");
        negcheck ("1.2 .3");
        negcheck ("1.2.3 ");

        // leading zeroes
        negcheck ("01.2.3");
        negcheck ("1.02.3");
        negcheck ("1.2.03");
    }

    static identifier_list ids ()
    {
        return identifier_list ();
    }

    static identifier_list ids (
        std::string const& s1)
    {
        identifier_list v;
        v.push_back (s1);
        return v;
    }

    static identifier_list ids (
        std::string const& s1, std::string const& s2)
    {
        identifier_list v;
        v.push_back (s1);
        v.push_back (s2);
        return v;
    }

    static identifier_list ids (
        std::string const& s1, std::string const& s2, std::string const& s3)
    {
        identifier_list v;
        v.push_back (s1);
        v.push_back (s2);
        v.push_back (s3);
        return v;
    }

    // Checks the decomposition of the input into appropriate values
    void checkValues (std::string const& input,
        int majorVersion,
        int minorVersion,
        int patchVersion,
        identifier_list const& preReleaseIdentifiers = identifier_list (),
        identifier_list const& metaData = identifier_list ())
    {
        SemanticVersion v;

        expect (v.parse (input));

        expect (v.majorVersion == majorVersion);
        expect (v.minorVersion == minorVersion);
        expect (v.patchVersion == patchVersion);

        expect (v.preReleaseIdentifiers == preReleaseIdentifiers);
        expect (v.metaData == metaData);
    }

    void testValues ()
    {
        testcase ("values");

        checkValues ("0.1.2", 0, 1, 2);
        checkValues ("1.2.3", 1, 2, 3);
        checkValues ("1.2.3-rc1", 1, 2, 3, ids ("rc1"));
        checkValues ("1.2.3-rc1.debug", 1, 2, 3, ids ("rc1", "debug"));
        checkValues ("1.2.3-rc1.debug.asm", 1, 2, 3, ids ("rc1", "debug", "asm"));
        checkValues ("1.2.3+full", 1, 2, 3, ids (), ids ("full"));
        checkValues ("1.2.3+full.prod", 1, 2, 3, ids (), ids ("full", "prod"));
        checkValues ("1.2.3+full.prod.x86", 1, 2, 3, ids (), ids ("full", "prod", "x86"));
        checkValues ("1.2.3-rc1.debug.asm+full.prod.x86", 1, 2, 3,
            ids ("rc1", "debug", "asm"), ids ("full", "prod", "x86"));
    }

    // makes sure the left version is less than the right
    void checkLessInternal (std::string const& lhs, std::string const& rhs)
    {
        SemanticVersion left;
        SemanticVersion right;

        expect (left.parse (lhs));
        expect (right.parse (rhs));

        expect (compare (left, left) == 0);
        expect (compare (right, right) == 0);
        expect (compare (left, right) < 0);
        expect (compare (right, left) > 0);

        expect (left < right);
        expect (right > left);
        expect (left == left);
        expect (right == right);
    }

    void checkLess (std::string const& lhs, std::string const& rhs)
    {
        checkLessInternal (lhs, rhs);
        checkLessInternal (lhs + "+meta", rhs);
        checkLessInternal (lhs, rhs + "+meta");
        checkLessInternal (lhs + "+meta", rhs + "+meta");
    }

    void testCompare ()
    {
        testcase ("comparisons");

        checkLess ("1.0.0-alpha", "1.0.0-alpha.1");
        checkLess ("1.0.0-alpha.1", "1.0.0-alpha.beta");
        checkLess ("1.0.0-alpha.beta", "1.0.0-beta");
        checkLess ("1.0.0-beta", "1.0.0-beta.2");
        checkLess ("1.0.0-beta.2", "1.0.0-beta.11");
        checkLess ("1.0.0-beta.11", "1.0.0-rc.1");
        checkLess ("1.0.0-rc.1", "1.0.0");
        checkLess ("0.9.9", "1.0.0");
    }

    void run ()
    {
        testParse ();
        testValues ();
        testCompare ();
    }
};

BEAST_DEFINE_TESTSUITE(SemanticVersion,beast_core,beast);

} // beast
