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

namespace beast {

SemanticVersion::SemanticVersion ()
    : majorVersion (0)
    , minorVersion (0)
    , patchVersion (0)
{
}

bool SemanticVersion::parse (String input)
{
    // May not have leading or trailing whitespace
    if (input.trim () != input)
        return false;

    // Must have major version number
    if (! chopUInt (&majorVersion, std::numeric_limits <int>::max (), input))
        return false;

    if (! chop (".", input))
        return false;

    // Must have minor version number
    if (! chopUInt (&minorVersion, std::numeric_limits <int>::max (), input))
        return false;

    if (! chop (".", input))
        return false;

    // Must have patch version number
    if (! chopUInt (&patchVersion, std::numeric_limits <int>::max (), input))
        return false;

    // May have pre-release identifier list
    if (chop ("-", input))
    {
        chopIdentifiers (&preReleaseIdentifiers, false, input);

        // Must not be empty
        if (preReleaseIdentifiers.size () <= 0)
            return false;
    }

    // May have metadata identifier list
    if (chop ("+", input))
    {
        chopIdentifiers (&metaData, true, input);

        // Must not be empty
        if (metaData.size () <= 0)
            return false;
    }

    // May not have anything left
    if (input.length () > 0)
        return false;

    return true;
}

String SemanticVersion::print () const
{
    String s;

    s << String (majorVersion) << "." << String (minorVersion) << "." << String (patchVersion);

    if (preReleaseIdentifiers.size () > 0)
        s << "-" << printIdentifiers (preReleaseIdentifiers);

    if (metaData.size () > 0)
        s << "+" << printIdentifiers (metaData);

    return s;
}

int SemanticVersion::compare (SemanticVersion const& rhs) const noexcept
{
    if (majorVersion > rhs.majorVersion)
        return 1;
    else if (majorVersion < rhs.majorVersion)
        return -1;

    if (minorVersion > rhs.minorVersion)
        return 1;
    else if (minorVersion < rhs.minorVersion)
        return -1;

    if (patchVersion > rhs.patchVersion)
        return 1;
    else if (patchVersion < rhs.patchVersion)
        return -1;

    if (isPreRelease () || rhs.isPreRelease ())
    {
        // Pre-releases have a lower precedence
        if (isRelease () && rhs.isPreRelease ())
            return 1;
        else if (isPreRelease () && rhs.isRelease ())
            return -1;

        // Compare pre-release identifiers
        for (int i = 0; i < bmax (preReleaseIdentifiers.size (), rhs.preReleaseIdentifiers.size ()); ++i)
        {
            // A larger list of identifiers has a higher precedence
            if (i >= rhs.preReleaseIdentifiers.size ())
                return 1;
            else if (i >= preReleaseIdentifiers.size ())
                return -1;

            String const& left (preReleaseIdentifiers [i]);
            String const& right (rhs.preReleaseIdentifiers [i]);

            // Numeric identifiers have lower precedence
            if (! isNumeric (left) && isNumeric (right))
                return 1;
            else if (isNumeric (left) && ! isNumeric (right))
                return -1;

            if (isNumeric (left))
            {
                bassert (isNumeric (right));

                int const iLeft (left.getIntValue ());
                int const iRight (right.getIntValue ());

                if (iLeft > iRight)
                    return 1;
                else if (iLeft < iRight)
                    return -1;
            }
            else
            {
                bassert (! isNumeric (right));

                int result = left.compareLexicographically (right);

                if (result != 0)
                    return result;
            }
        }
    }

    // metadata is ignored

    return 0;
}

bool SemanticVersion::isNumeric (String const& s)
{
    return String (s.getIntValue ()) == s;
}

bool SemanticVersion::chop (String const& what, String& input)
{
    if (input.startsWith (what))
    {
        input = input.substring (what.length ());

        return true;
    }

    return false;
}

String SemanticVersion::printIdentifiers (StringArray const& list)
{
    String s;

    if (list.size () > 0)
    {
        s << list [0];

        for (int i = 1; i < list.size (); ++i)
            s << "." << list [i];
    }

    return s;
}

bool SemanticVersion::chopUInt (int* value, int limit, String& input)
{
    // Must not be empty
    if (input.length () <= 0)
        return false;

    int firstNonDigit = 0;
    for (; firstNonDigit < input.length (); ++firstNonDigit)
    {
        if (! CharacterFunctions::isDigit (input [firstNonDigit]))
            break;
    }

    String const s = input.substring (0, firstNonDigit);

    // Must not be empty
    if (s.length () <= 0)
        return false;

    int const n = s.getIntValue ();

    // Must not have leading zeroes
    if (String (n) != s)
        return false;

    // Must not be out of range
    if (n < 0 || n > limit)
        return false;

    input = input.substring (s.length ());

    *value = n;

    return true;
}

bool SemanticVersion::chopIdentifier (String* value, bool allowLeadingZeroes, String& input)
{
    // Must not be empty
    if (input.length () <= 0)
        return false;

    // Must not have a leading 0
    if (! allowLeadingZeroes && input [0] == '0')
        return false;

    // Find the first character that cannot be part of an identifier
    int i;
    for (i = 0; i < input.length (); ++i)
    {
        static char const* validSet =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";

        if (! String (validSet).contains (String::charToString (input [i])))
            break;
    }

    // Must not be empty
    if (i <= 0)
        return false;

    *value = input.substring (0, i);

    input = input.substring (i);

    return true;
}

bool SemanticVersion::chopIdentifiers (StringArray* value, bool allowLeadingZeroes, String& input)
{
    if (input.length () <= 0)
        return false;

    for (;;)
    {
        String s;

        if (! chopIdentifier (&s, allowLeadingZeroes, input))
            return false;

        value->add (s);

        if (! chop (".", input))
            break;
    }

    return true;
}

//------------------------------------------------------------------------------

class SemanticVersion_test: public unit_test::suite
{
public:
    void checkPass (String const& input, bool shouldPass = true)
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

    void checkFail (String const& input)
    {
        checkPass (input, false);
    }

    // check input and input with appended metadata
    void checkMeta (String const& input, bool shouldPass)
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

    void checkMetaFail (String const& input)
    {
        checkMeta (input, false);
    }

    // check input, input with appended release data,
    // input with appended metadata, and input with both
    // appended release data and appended metadata
    //
    void checkRelease (String const& input, bool shouldPass = true)
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
    void check (String const& input, bool shouldPass = true)
    {
        checkRelease (input, shouldPass);
    }

    void negcheck (String const& input)
    {
        check (input, false);
    }

    void testParse ()
    {
        testcase ("parse");

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

    static StringArray ids ()
    {
        return StringArray ();
    }

    static StringArray ids (String const& s1)
    {
        StringArray v;
        v.add (s1);
        return v;
    }

    static StringArray ids (String const& s1, String const& s2)
    {
        StringArray v;
        v.add (s1);
        v.add (s2);
        return v;
    }

    static StringArray ids (String const& s1, String const& s2, String const& s3)
    {
        StringArray v;
        v.add (s1);
        v.add (s2);
        v.add (s3);
        return v;
    }

    // Checks the decomposition of the input into appropriate values
    void checkValues (String const& input,
                      int majorVersion,
                      int minorVersion,
                      int patchVersion,
                      StringArray const& preReleaseIdentifiers = StringArray (),
                      StringArray const& metaData = StringArray ())
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
    void checkLessInternal (String const& lhs, String const& rhs)
    {
        SemanticVersion left;
        SemanticVersion right;

        expect (left.parse (lhs));
        expect (right.parse (rhs));

        expect (left.compare (left) == 0);
        expect (right.compare (right) == 0);
        expect (left.compare (right) < 0);
        expect (right.compare (left) > 0);

        expect (left < right);
        expect (right > left);
        expect (left == left);
        expect (right == right);
    }

    void checkLess (String const& lhs, String const& rhs)
    {
        checkLessInternal (lhs, rhs);
        checkLessInternal (lhs + "+meta", rhs);
        checkLessInternal (lhs, rhs + "+meta");
        checkLessInternal (lhs + "+meta", rhs + "+meta");
    }

    void testCompare ()
    {
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
