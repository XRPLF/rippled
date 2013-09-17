//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

LogSink::LogSink ()
    : m_mutex ("Log", __FILE__, __LINE__)
    , m_minSeverity (lsINFO)
{
}

LogSink::~LogSink ()
{
}

LogSeverity LogSink::getMinSeverity ()
{
    ScopedLockType lock (m_mutex, __FILE__, __LINE__);

    return m_minSeverity;
}

void LogSink::setMinSeverity (LogSeverity s, bool all)
{
    ScopedLockType lock (m_mutex, __FILE__, __LINE__);

    m_minSeverity = s;

    if (all)
        LogPartition::setSeverity (s);
}

void LogSink::setLogFile (boost::filesystem::path const& path)
{
    bool const wasOpened = m_logFile.open (path.c_str ());

    if (! wasOpened)
    {
        Log (lsFATAL) << "Unable to open logfile " << path;
    }
}

std::string LogSink::rotateLog ()
{
    ScopedLockType lock (m_mutex, __FILE__, __LINE__);

    bool const wasOpened = m_logFile.closeAndReopen ();

    if (wasOpened)
    {
        return "The log file was closed and reopened.";
    }
    else
    {
        return "The log file could not be closed and reopened.";
    }
}

void LogSink::write (
    std::string const& message,
    LogSeverity severity,
    std::string const& partitionName)
{
    std::string text;
    text.reserve (message.size() + partitionName.size() + 100);

    text = boost::posix_time::to_simple_string (
        boost::posix_time::second_clock::universal_time ());

    text += " ";
    if (! partitionName.empty ())
        text += partitionName + ":";

    switch (severity)
    {
    case lsTRACE:       text += "TRC "; break;
    case lsDEBUG:       text += "DBG "; break;
    case lsINFO:        text += "NFO "; break;
    case lsWARNING:     text += "WRN "; break;
    case lsERROR:       text += "ERR "; break;
    default:
        bassertfalse;
    case lsFATAL:       text += "FTL ";
        break;
    }

    text += replaceFirstSecretWithAsterisks (message);

    if (text.size() > maximumMessageCharacters)
    {
        text.resize (maximumMessageCharacters - 3);
        text += "...";
    }

    {
        ScopedLockType lock (m_mutex, __FILE__, __LINE__);

        write (text, severity >= getMinSeverity(), lock);
    }
}

void LogSink::write (std::string const& text)
{
    ScopedLockType lock (m_mutex, __FILE__, __LINE__);

    write (text, true, lock);
}

void LogSink::write (StringArray const& strings)
{
    ScopedLockType lock (m_mutex, __FILE__, __LINE__);

    for (int i = 0; i < strings.size (); ++i)
        write (strings [i].toStdString (), true, lock);

}

void LogSink::write (std::string const& line, bool toStdErr, ScopedLockType&)
{
    // Does nothing if not open.
    m_logFile.writeln (line);

    if (toStdErr)
    {
#if BEAST_MSVC
        if (beast_isRunningUnderDebugger ())
        {
            // Send it to the attached debugger's Output window
            //
            Logger::outputDebugString (line);
        }
        else
#endif
        {
            std::cerr << line << std::endl;
        }
    }
}

//------------------------------------------------------------------------------

std::string LogSink::replaceFirstSecretWithAsterisks (std::string s)
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

LogSink::Ptr LogSink::get ()
{
    return SharedSingleton <LogSink>::getInstance ();
}

