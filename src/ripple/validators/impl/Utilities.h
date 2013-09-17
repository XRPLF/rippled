//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORS_UTILITIES_H_INCLUDED
#define RIPPLE_VALIDATORS_UTILITIES_H_INCLUDED

namespace Validators
{

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

    // conversion between PublicKey and String
    static std::string publicKeyToString (PublicKey const& publicKey);
    static PublicKey stringToPublicKey (std::string const& s);

    struct Helpers;

    /** Parse a string into a Source::Info.
        @return `true` on success.
    */
    static bool parseInfoLine (
        Source::Info& info, std::string const& line, Journal journal);
};

}

#endif
