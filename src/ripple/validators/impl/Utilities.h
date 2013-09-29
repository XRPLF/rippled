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

/** Common code for Validators classes.
*/
class Utilities
{
public:
    typedef std::vector <std::string> Strings;

#if 0
    /** Parse a ConstBufferSequence of newline delimited text into strings.
        This works incrementally.
    */
    template <typename ConstBufferSequence>
    static void parseLines (Strings& lines, ConstBufferSequence const& buffers)
    {
        for (typename ConstBufferSequence::const_iterator iter = buffers.begin ();
            iter != buffers.end (); ++iter)
            parserLines (lines, *iter);
    }

    /** Turn a linear buffer of newline delimited text into strings.
        This can be called incrementally, i.e. successive calls with
        multiple buffer segments.
    */
    static void parseLines (Strings& lines, char const* buf, std::size_t bytes);
#endif

    /** Parse a string into the Source::Result.
        Invalid or comment lines will be skipped.
        Lines containing validator info will be added to the Result object.
        Metadata lines will update the corresponding Result fields.
    */
    static void parseResultLine (
        Source::Result& result,
        std::string const& line,
        Journal journal = Journal());

    // helpers
    static String itos (int i, int fieldSize = 0);
    static int stoi (String& s, int fieldSize, int minValue, int maxValue, beast_wchar delimiter);

    // conversion betwen Time and String
    static String timeToString (Time const& t);
    static Time stringToTime (String s);

    // conversion between RipplePublicKey and String
    static std::string publicKeyToString (RipplePublicKey const& publicKey);
    static RipplePublicKey stringToPublicKey (std::string const& s);

    struct Helpers;

    /** Parse a string into a Source::Info.
        @return `true` on success.
    */
    static bool parseInfoLine (
        Source::Info& info, std::string const& line, Journal journal);
};

}
}

#endif
