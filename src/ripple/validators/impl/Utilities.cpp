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

}
