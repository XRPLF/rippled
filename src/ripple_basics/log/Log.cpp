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


std::string Log::replaceFirstSecretWithAsterisks (std::string s)
{
    using namespace std;

    char const* secretToken = "\"secret\"";

    // Look for the first occurrence of "secret" in the string.
    //
    size_t startingPosition = s.find (secretToken);

    if (startingPosition != string::npos)
    {
        // Found it, advance past the token.
        //
        startingPosition += strlen (secretToken);

        // Replace the next 35 characters at most, without overwriting the end.
        //
        size_t endingPosition = std::min (startingPosition + 35, s.size () - 1);

        for (size_t i = startingPosition; i < endingPosition; ++i)
            s [i] = '*';
    }

    return s;
}

//------------------------------------------------------------------------------

Log::~Log ()
{
    LogSink::get()->write (oss.str(), mSeverity, mPartitionName);
}

std::string Log::severityToString (LogSeverity s)
{
    switch (s)
    {
    case lsTRACE:
        return "Trace";

    case lsDEBUG:
        return "Debug";

    case lsINFO:
        return "Info";

    case lsWARNING:
        return "Warning";

    case lsERROR:
        return "Error";

    case lsFATAL:
        return "Fatal";

    default:
        assert (false);
        return "Unknown";
    }
}

LogSeverity Log::stringToSeverity (const std::string& s)
{
    if (boost::iequals (s, "trace"))
        return lsTRACE;

    if (boost::iequals (s, "debug"))
        return lsDEBUG;

    if (boost::iequals (s, "info") || boost::iequals (s, "information"))
        return lsINFO;

    if (boost::iequals (s, "warn") || boost::iequals (s, "warning") || boost::iequals (s, "warnings"))
        return lsWARNING;

    if (boost::iequals (s, "error") || boost::iequals (s, "errors"))
        return lsERROR;

    if (boost::iequals (s, "fatal") || boost::iequals (s, "fatals"))
        return lsFATAL;

    return lsINVALID;
}
