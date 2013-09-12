//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_VALIDATORSUTILITIES_H_INCLUDED
#define RIPPLE_CORE_VALIDATORSUTILITIES_H_INCLUDED

/** Common code for Validators classes.
*/
class ValidatorsUtilities
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
        Validators::Source::Result& result,
        String line);

private:
    /** Parse a string into a Source::Info.
        @return `true` on success.
    */
    static bool parseInfoLine (Validators::Source::Info& info, String line);
};

#endif
