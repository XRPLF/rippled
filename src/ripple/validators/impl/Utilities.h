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

#ifndef RIPPLE_VALIDATORS_UTILITIES_H_INCLUDED
#define RIPPLE_VALIDATORS_UTILITIES_H_INCLUDED

namespace ripple {
namespace Validators {

/** Common code for Validators classes. */
class Utilities
{
public:
    typedef std::vector <std::string> Strings;

    /** A suitable LineFunction for parsing items into a fetch results. */
    class ParseResultLine
    {
    public:
        ParseResultLine (Source::Results& results, beast::Journal journal)
            : m_result (&results)
            , m_journal (journal)
        { }

        template <typename BidirectionalIterator>
        void operator() (BidirectionalIterator first, BidirectionalIterator last)
        {
            std::string s (first, last);
            Utilities::parseResultLine (*m_result, s, m_journal);
        }

    private:
        Source::Results* m_result;
        beast::Journal m_journal;
    };

    /** UnaryPredicate for breaking up lines.
        This returns `true` for the first non-vertical whitespace character that
        follows a vertical whitespace character.
    */
    class FollowingVerticalWhite
    {
    public:
        FollowingVerticalWhite ()
            : m_gotWhite (false)
        {
        }

        template <typename CharT>
        static bool isVerticalWhitespace (CharT c)
        {
            return c == '\r' || c == '\n';
        }

        template <typename CharT>
        bool operator() (CharT c)
        {
            if (isVerticalWhitespace (c))
            {
                m_gotWhite = true;
                return false;
            }
            else if (m_gotWhite)
            {
                m_gotWhite = false;
                return true;
            }
            return false;
        }

    private:
        bool m_gotWhite;
    };

    /** Call LineFunction for each newline-separated line in the input.
        LineFunction will be called with this signature:
        @code
            void LineFunction (BidirectionalIterator first, BidirectionalIterator last)
        @endcode
        Where first and last mark the beginning and ending of the line.
        The last line in the input may or may not contain the trailing newline.
    */
    template <typename BidirectionalIterator, typename LineFunction>
    static void processLines (BidirectionalIterator first,
        BidirectionalIterator last, LineFunction f)
    {
        for (;;)
        {
            BidirectionalIterator split (std::find_if (
                first, last, FollowingVerticalWhite ()));
            f (first, split);
            if (split == last)
                break;
            first = split;
        }
    }

    /** Parse a string into the Source::Results.
        Invalid or comment lines will be skipped.
        Lines containing validator info will be added to the Results object.
        Metadata lines will update the corresponding Results fields.
    */
    static void parseResultLine (
        Source::Results& results,
        std::string const& line,
        beast::Journal journal = beast::Journal());

    // helpers
    static beast::String itos (int i, int fieldSize = 0);
    static int stoi (beast::String& s, int fieldSize, int minValue, int maxValue,
                     beast::beast_wchar delimiter);

    // conversion betwen Time and String
    static beast::String timeToString (beast::Time const& t);
    static beast::Time stringToTime (beast::String s);

    struct Helpers;

    /** Parse a string into a Source::Item.
        @return `true` on success.
    */
    static bool parseInfoLine (
        Source::Item& item, std::string const& line, beast::Journal journal);
};

}
}

#endif
