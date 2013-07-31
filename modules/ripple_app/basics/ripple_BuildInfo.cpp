//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

String const& BuildInfo::getVersionString ()
{
    static char const* const rawText =
        
    //--------------------------------------------------------------------------
    //
    // The build version number (edit this for each release)
    //
        "0.010-rc1"
    //
    //--------------------------------------------------------------------------
    ;

    struct StaticInitializer
    {
        StaticInitializer ()
        {
            // Sanity checking on the raw text

            Version v;

            if (! v.parse (rawText) || v.print () != rawText)
                Throw (std::invalid_argument ("illegal server version format string"));

            versionString = rawText;
        }

        String versionString;
    };

    static StaticInitializer value;

    return value.versionString;
}

// The protocol version we speak and prefer.
//
BuildInfo::Protocol const& BuildInfo::getCurrentProtocol ()
{
    static Protocol currentProtocol (1, 2);

    return currentProtocol;
}

// The oldest protocol version we will accept.
//
BuildInfo::Protocol const& BuildInfo::getMinimumProtocol ()
{
    static Protocol minimumProtocol (1, 2);

    return minimumProtocol;
}



// Don't touch anything below this line
//------------------------------------------------------------------------------

char const* BuildInfo::getFullVersionString ()
{
    struct StaticInitializer
    {
        StaticInitializer ()
        {
            String s;
            
            s << "Ripple-" << getVersionString ();

            fullVersionString = s.toStdString ();
        }

        std::string fullVersionString;
    };

    static StaticInitializer value;

    return value.fullVersionString.c_str ();
}

//------------------------------------------------------------------------------

BuildInfo::Version::Version ()
    : vmajor (0)
    , vminor (0)
{
}

bool BuildInfo::Version::parse (String const& s)
{
    // Many not have leading or trailing whitespace
    if (s.trim () != s)
        return false;

    int const indexOfDot = s.indexOfChar ('.');

    // Must have a dot
    if (indexOfDot == -1)
        return false;

    String const majorString = s.substring (0, indexOfDot);

    // Must only contain digits
    if (! majorString.containsOnly ("0123456789"))
        return false;

    // Must match after conversion back and forth
    if (String (majorString.getIntValue ()) != majorString)
        return false;

    int const indexOfDash = s.indexOfChar ('-');

    // A dash must come after the dot.
    if (indexOfDash >= 0 && indexOfDash <= indexOfDot)
        return false;

    String const minorString = (indexOfDash == -1) ?
        s.substring (indexOfDot + 1) : s.substring (indexOfDot + 1, indexOfDash);

    // Must be length three
    if (minorString.length () != 3)
        return false;

    // Must only contain digits
    if (! minorString.containsOnly ("0123456789"))
        return false;

    String const suffixString = (indexOfDash == -1) ?
        "" : s.substring (indexOfDash + 1);

    if (suffixString.length () > 0)
    {
        // Must be 4 characters or less
        if (suffixString.length () > 4)
            return false;

        // Must start with a letter
        if (! String::charToString (suffixString [0]).containsOnly ("abcdefghijklmnopqrstuvwxyz"))
            return false;

        // Must only contain letters and numbers
        if (! String::charToString (suffixString [0]).containsOnly ("abcdefghijklmnopqrstuvwxyz01234567890"))
            return false;
    }

    vmajor = majorString.getIntValue ();
    vminor = minorString.getIntValue ();
    suffix = suffixString;

    return true;
}

String BuildInfo::Version::print () const noexcept
{
    String s;

    s << String (vmajor) << "." << String (vminor).paddedLeft ('0', 3);

    if (suffix.isNotEmpty ())
        s << "-" << suffix;

    return s;
}

//------------------------------------------------------------------------------

BuildInfo::Protocol::Protocol ()
    : vmajor (0)
    , vminor (0)
{
}

BuildInfo::Protocol::Protocol (unsigned short major_, unsigned short minor_)
    : vmajor (major_)
    , vminor (minor_)
{
}

BuildInfo::Protocol::Protocol (PackedFormat packedVersion)
{
    vmajor = (packedVersion >> 16) & 0xffff;
    vminor = (packedVersion & 0xffff);
}

BuildInfo::Protocol::PackedFormat BuildInfo::Protocol::toPacked () const noexcept
{
    return ((vmajor << 16) & 0xffff0000) | (vminor & 0xffff);
}

std::string BuildInfo::Protocol::toStdString () const noexcept
{
    String s;

    s << String (vmajor) << "." << "vminor";

    return s.toStdString ();
}

//------------------------------------------------------------------------------

class BuildInfoTests : public UnitTest
{
public:
    BuildInfoTests () : UnitTest ("BuildInfo", "ripple")
    {
    }

    void checkVersion (String const& s)
    {
        BuildInfo::Version v;

        expect (v.parse (s));

        // Conversion back and forth should be identical
        expect (v.print () == s);
    }

    void testVersion ()
    {
        beginTestCase ("version");

        BuildInfo::Version v;

        checkVersion ("0.000");
        checkVersion ("1.002");
        checkVersion ("10.002");
        checkVersion ("99.999");
        checkVersion ("99.999-r");
        checkVersion ("99.999-r1");
        checkVersion ("99.999-r123");

        unexpected (v.parse (" 1.2"));      // Many not have leading or trailing whitespace
        unexpected (v.parse ("1.2 "));      // Many not have leading or trailing whitespace
        unexpected (v.parse (" 1.2 "));     // Many not have leading or trailing whitespace
        unexpected (v.parse ("2"));         // Must have a dot
        unexpected (v.parse ("23"));        // Must have a dot
        unexpected (v.parse ("4-rc1"));     // Must have a dot
        unexpected (v.parse ("01.000"));    // No leading zeroes
        unexpected (v.parse ("4-4.r"));     // A dash must come after the dot.
        unexpected (v.parse ("1.2345"));    // Must be length three
        unexpected (v.parse ("1a.2"));      // Must only contain digits
        unexpected (v.parse ("1.2b"));      // Must only contain digits
        unexpected (v.parse ("1.2-rxxx1")); // Must be 4 characters or less
        unexpected (v.parse ("1.2-"));      // Must start with a letter
        unexpected (v.parse ("1.2-3"));     // Must start with a letter
        unexpected (v.parse ("1.2-r!"));    // Must only contain letters and numbers
    }

    void checkProtcol (unsigned short vmajor, unsigned short vminor)
    {
        typedef BuildInfo::Protocol P;

        expect (P (P (vmajor, vminor).toPacked ()) == P (vmajor, vminor));
    }

    void testProtocol ()
    {
        typedef BuildInfo::Protocol P;

        beginTestCase ("protocol");

        expect (P (0, 0).toPacked () == 0);
        expect (P (0, 1).toPacked () == 1);
        expect (P (0, 65535).toPacked () == 65535);

        checkProtcol (0, 0);
        checkProtcol (0, 1);
        checkProtcol (0, 255);
        checkProtcol (0, 65535);
        checkProtcol (1, 0);
        checkProtcol (1, 65535);
        checkProtcol (65535, 65535);
    }

    void testValues ()
    {
        beginTestCase ("comparison");

        typedef BuildInfo::Protocol P;

        expect (P(1,2) == P(1,2));
        expect (P(3,4) >= P(3,4));
        expect (P(5,6) <= P(5,6));
        expect (P(7,8) >  P(6,7));
        expect (P(7,8) <  P(8,9));
        expect (P(65535,0) <  P(65535,65535));
        expect (P(65535,65535) >= P(65535,65535));

        expect (BuildInfo::getCurrentProtocol () >= BuildInfo::getMinimumProtocol ());
    }

    void runTest ()
    {
        testVersion ();
        testProtocol ();
        testValues ();
    }
};

static BuildInfoTests buildInfoTests;
