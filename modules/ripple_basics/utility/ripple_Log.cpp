//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

LogFile Log::s_logFile;
Log::StaticLockType Log::s_lock ("Log", __FILE__, __LINE__);
LogSeverity Log::sMinSeverity = lsINFO;

//------------------------------------------------------------------------------

LogPartition* LogPartition::headLog = NULL;

LogPartition::LogPartition (char const* partitionName)
    : mNextLog (headLog)
    , mMinSeverity (lsWARNING)
{
    const char* ptr = strrchr (partitionName, '/');
    mName = (ptr == NULL) ? partitionName : (ptr + 1);

    size_t p = mName.find (".cpp");

    if (p != std::string::npos)
        mName.erase (mName.begin () + p, mName.end ());

    headLog = this;
}

std::vector< std::pair<std::string, std::string> > LogPartition::getSeverities ()
{
    std::vector< std::pair<std::string, std::string> > sevs;

    for (LogPartition* l = headLog; l != NULL; l = l->mNextLog)
        sevs.push_back (std::make_pair (l->mName, Log::severityToString (l->mMinSeverity)));

    return sevs;
}

//------------------------------------------------------------------------------

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
    std::string logMsg = boost::posix_time::to_simple_string (boost::posix_time::second_clock::universal_time ());

    if (!mPartitionName.empty ())
        logMsg += " " + mPartitionName + ":";
    else
        logMsg += " ";

    switch (mSeverity)
    {
    case lsTRACE:
        logMsg += "TRC ";
        break;

    case lsDEBUG:
        logMsg += "DBG ";
        break;

    case lsINFO:
        logMsg += "NFO ";
        break;

    case lsWARNING:
        logMsg += "WRN ";
        break;

    case lsERROR:
        logMsg += "ERR ";
        break;

    case lsFATAL:
        logMsg += "FTL ";
        break;

    case lsINVALID:
        assert (false);
        return;
    }

    logMsg += replaceFirstSecretWithAsterisks (oss.str ());

    if (logMsg.size () > maximumMessageCharacters)
    {
        logMsg.resize (maximumMessageCharacters);
        logMsg += "...";
    }

    print (logMsg, mSeverity >= sMinSeverity);
}

void Log::print (std::string const& text, bool toStdErr)
{
    StaticScopedLockType sl (s_lock, __FILE__, __LINE__);

    // Does nothing if not open.
    s_logFile.writeln (text);

    if (toStdErr)
    {
#if 0 //BEAST_MSVC
        if (beast_isRunningUnderDebugger ())
        {
            // Send it to the attached debugger's Output window
            //
            Logger::outputDebugString (text);
        }
        else
#endif
        {
            std::cerr << text << std::endl;
        }
    }
}

void Log::print (StringArray const& strings, bool toStdErr)
{
    StaticScopedLockType sl (s_lock, __FILE__, __LINE__);

    for (int i = 0; i < strings.size (); ++i)
        print (strings [i].toStdString (), toStdErr);

}

std::string Log::rotateLog ()
{
    StaticScopedLockType sl (s_lock, __FILE__, __LINE__);

    bool const wasOpened = s_logFile.closeAndReopen ();

    if (wasOpened)
    {
        return "The log file was closed and reopened.";
    }
    else
    {
        return "The log file could not be closed and reopened.";
    }
}

void Log::setMinSeverity (LogSeverity s, bool all)
{
    StaticScopedLockType sl (s_lock, __FILE__, __LINE__);

    sMinSeverity = s;

    if (all)
        LogPartition::setSeverity (s);
}

LogSeverity Log::getMinSeverity ()
{
    StaticScopedLockType sl (s_lock, __FILE__, __LINE__);

    return sMinSeverity;
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

void Log::setLogFile (boost::filesystem::path const& path)
{
    bool const wasOpened = s_logFile.open (path.c_str ());

    if (! wasOpened)
    {
        Log (lsFATAL) << "Unable to open logfile " << path;
    }
}

bool LogPartition::setSeverity (const std::string& partition, LogSeverity severity)
{
    for (LogPartition* p = headLog; p != NULL; p = p->mNextLog)
        if (boost::iequals (p->mName, partition))
        {
            p->mMinSeverity = severity;
            return true;
        }

    return false;
}

void LogPartition::setSeverity (LogSeverity severity)
{
    for (LogPartition* p = headLog; p != NULL; p = p->mNextLog)
        p->mMinSeverity = severity;
}

// vim:ts=4
