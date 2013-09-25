//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
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
