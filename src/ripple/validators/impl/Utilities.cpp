//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace Validators
{

struct Utilities::Helpers
{
    // Matches a validator info line.
    //
    static boost::regex& reInfo ()
    {
        // e.g.
        //
        // n9KorY8QtTdRx7TVDpwnG9NvyxsDwHUKUEeDLY3AkiGncVaSXZi5 Comment Text
        //
        static boost::regex re (
            "^"                 //     start of line
            "(?:\\h*)"          //     horiz-white (optional)
            "([^\\h\\v]+)"      // [1] non-white run
            "(?:\\h*)"          //     horiz-white (optional)
            "([^\\h\\v]*)"      // [2] any text (optional)
            "$"                 //     end of line

            , boost::regex::perl |
              boost::regex_constants::match_flags::match_not_dot_null
        );

        return re;
    }

    // Matches a comment or whitespace line.
    //
    static boost::regex& reComment ()
    {
        // e.g.
        //
        // n9KorY8QtTdRx7TVDpwnG9NvyxsDwHUKUEeDLY3AkiGncVaSXZi5 Comment Text
        //
        static boost::regex re (
            "^"                 //     start of line
            "(?:\\h*)"          //     horiz-white (optional)
            "(?:#.*)"           //     # then any text
            "|"                 //     - or -
            "(?:\\h*)"          //     horiz-white
            "$"                 //     end of line

            , boost::regex::perl |
              boost::regex_constants::match_flags::match_not_dot_null
        );

        return re;
    }
};

//------------------------------------------------------------------------------

bool Utilities::parseInfoLine (
    Source::Info& info,
        std::string const& line,
            Journal journal)
{
    bool success (false);

    boost::smatch match;

    if (boost::regex_match (line, match, Helpers::reInfo ()))
    {
        std::string const encodedKey (match [1]);
        std::string const commentText (match [2]);

        RippleAddress deprecatedPublicKey;

        // VFALCO NOTE These bool return values are poorlydocumented
        //
        if (deprecatedPublicKey.setSeedGeneric (encodedKey))
        {
            // expected a domain or public key but got a generic seed?
            // log?
        }
        else if (deprecatedPublicKey.setNodePublic (encodedKey))
        {
            // We got a public key.
            RipplePublicKey publicKey (deprecatedPublicKey.toRipplePublicKey ());
            success = true;
        }
        else
        {
            // Some other junk.
            // log?
        }
    }
    else if (boost::regex_match (line, match, Helpers::reComment ()))
    {
        // it's a comment
    }
    else
    {
        // Log a warning about a parsing error
    }

#if 0
    static boost::regex reReferral ("\\`\\s*(\\S+)(?:\\s+(.+))?\\s*\\'");

    if (!boost::regex_match (strReferral, smMatch, reReferral))
    {
        WriteLog (lsWARNING, UniqueNodeList) << str (boost::format ("Bad validator: syntax error: %s: %s") % strSite % strReferral);
    }
    else
    {
        std::string     strRefered  = smMatch[1];
        std::string     strComment  = smMatch[2];
        RippleAddress   naValidator;

        if (naValidator.setSeedGeneric (strRefered))
        {

            WriteLog (lsWARNING, UniqueNodeList) << str (boost::format ("Bad validator: domain or public key required: %s %s") % strRefered % strComment);
        }
        else if (naValidator.setNodePublic (strRefered))
        {
            // A public key.
            // XXX Schedule for CAS lookup.
            nodeAddPublic (naValidator, vsWhy, strComment);

            WriteLog (lsINFO, UniqueNodeList) << str (boost::format ("Node Public: %s %s") % strRefered % strComment);

            if (naNodePublic.isValid ())
                vstrValues.push_back (str (boost::format ("('%s',%d,'%s')") % strNodePublic % iValues % naValidator.humanNodePublic ()));

            iValues++;
        }
        else
        {
            // A domain: need to look it up.
            nodeAddDomain (strRefered, vsWhy, strComment);

            WriteLog (lsINFO, UniqueNodeList) << str (boost::format ("Node Domain: %s %s") % strRefered % strComment);

            if (naNodePublic.isValid ())
                vstrValues.push_back (str (boost::format ("('%s',%d,%s)") % strNodePublic % iValues % sqlEscape (strRefered)));

            iValues++;
        }
    }
#endif

    return success;
}

//------------------------------------------------------------------------------

void Utilities::parseResultLine (
    Source::Result& result,
        std::string const& line,
            Journal journal)
{
    bool success = false;

    if (! success)
    {
        Source::Info info;

        success = parseInfoLine (info, line, journal);
        if (success)
            result.list.add (info);
    }
}

//--------------------------------------------------------------------------

String Utilities::itos (int i, int fieldSize)
{
    return String::fromNumber (i).paddedLeft (beast_wchar('0'), fieldSize);
}

String Utilities::timeToString (Time const& t)
{
    if (t.isNotNull ())
    {
        return
            itos (t.getYear(), 4)        + "-" +
            itos (t.getMonth(), 2)       + "-" +
            itos (t.getDayOfMonth (), 2) + " " +
            itos (t.getHours () , 2)     + ":" +
            itos (t.getMinutes (), 2)    + ":" +
            itos (t.getSeconds(), 2);
    }

    return String::empty;
}

int Utilities::stoi (String& s, int fieldSize, int minValue, int maxValue, beast_wchar delimiter)
{
    int const needed (fieldSize + ((delimiter != 0) ? 1 : 0));
    String const v (s.substring (0, needed));
    s = s.substring (v.length ());
    if (s.length() == needed)
    {
        int const v (s.getIntValue());
        if (s.startsWith (itos (v, fieldSize)) &&
            v >= minValue && v <= maxValue &&
            (delimiter == 0 || s.endsWithChar (delimiter)))
        {
            return v;
        }
    }
    return -1; // fail
}

Time Utilities::stringToTime (String s)
{
    if (s.isNotEmpty ())
    {
        int const year (stoi (s, 4, 1970, 9999, '-'));
        int const mon  (stoi (s, 2,    0,   11, '-'));
        int const day  (stoi (s, 2,    1,   31, ' '));
        int const hour (stoi (s, 2,    0,   23, ':'));
        int const min  (stoi (s, 2,    0,   59, ':'));
        int const sec  (stoi (s, 2,    0,   59,  0 ));
        if (year != -1 &&
            mon  != -1 &&
            day  != -1 &&
            hour != -1 &&
            min  != -1 &&
            sec  != -1)
        {
            // local time
            return Time (year, mon, day, hour, min, sec, 0, true);
        }
    }

    return Time (0);
}

std::string Utilities::publicKeyToString (PublicKey const& publicKey)
{
    std::string s (PublicKey::sizeInBytes, ' ');
    std::copy (publicKey.cbegin(), publicKey.cend(), s.begin());
    return s;
}

PublicKey Utilities::stringToPublicKey (std::string const& s)
{
    bassert (s.size() == PublicKey::sizeInBytes);
    return PublicKey (&s.front());
}

//------------------------------------------------------------------------------

}
