//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

namespace ripple {
namespace Validators {

struct Utilities::Helpers
{
    // Matches a validator info line.
    //
    static boost::regex const& reInfo ()
    {
        // e.g.
        //
        // n9KorY8QtTdRx7TVDpwnG9NvyxsDwHUKUEeDLY3AkiGncVaSXZi5 Comment Text
        //
        static boost::regex re (
            "\\G"               //      end of last match (or start)
            "(?:[\\v\\h]*)"     //      white (optional)
            "([^\\h\\v]+)"      // [1]  non-white run
            "(?:\\h*)"          //      horiz-white (optional)
            "([^\\v]*?)"        // [2]  non vert-white text (optional)
            "(?:\\h*)"          //      white run (optional)
            "(?:\\v*)"          //      vert-white (optional)

            //"(?:\\')"           //      buffer boundary

            , boost::regex::perl
              //| boost::regex_constants::match_flags::match_not_dot_newline
        );

        return re;
    }

    // Matches a comment or whitespace line.
    //
    static boost::regex const& reComment ()
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
    Source::Item& item,
        std::string const& line,
            beast::Journal journal)
{
    bool success (false);

    boost::smatch match;

    if (boost::regex_match (line, match, Helpers::reInfo ()))
    {
        std::string const encodedKey (match [1]);
        std::string const commentText (match [2]);

        std::pair <RipplePublicKey, bool> results (
            RipplePublicKey::from_string (encodedKey));

        if (results.second)
        {
            // We got a public key.
            item.label = commentText;
            item.publicKey = results.first;
            success = true;
        }
        else
        {
            // Some other junk.
            journal.error << "Invalid RipplePublicKey: '" << encodedKey << "'";
        }
    }
    else if (boost::regex_match (line, match, Helpers::reComment ()))
    {
        // it's a comment
    }
    else
    {
        // Log a warning about a parsing error
        journal.error << "Invalid Validators source line:" << std::endl << line;
    }

    return success;
}

//------------------------------------------------------------------------------

void Utilities::parseResultLine (
    Source::Results& results,
        std::string const& line,
            beast::Journal journal)
{
    Source::Item item;

    bool success = parseInfoLine (item, line, journal);
    if (success)
    {
        results.list.push_back (item);
        results.success = true;
    }
}

//--------------------------------------------------------------------------

beast::String Utilities::itos (int i, int fieldSize)
{
    return beast::String::fromNumber (i).paddedLeft (beast::beast_wchar('0'), fieldSize);
}

beast::String Utilities::timeToString (beast::Time const& t)
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

    return beast::String::empty;
}

int Utilities::stoi (beast::String& s, int fieldSize, int minValue, int maxValue,
                     beast::beast_wchar delimiter)
{
    int const needed (fieldSize + ((delimiter != 0) ? 1 : 0));
    beast::String const v (s.substring (0, needed));
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

beast::Time Utilities::stringToTime (beast::String s)
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
            return beast::Time (year, mon, day, hour, min, sec, 0, true);
        }
    }

    return beast::Time (0);
}

//------------------------------------------------------------------------------

}
}
