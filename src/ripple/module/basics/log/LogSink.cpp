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

#include <beast/module/core/logging/Logger.h>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace ripple {

LogSink::LogSink ()
    : m_minSeverity (lsINFO)
{
}

LogSink::~LogSink ()
{
}

LogSeverity LogSink::getMinSeverity ()
{
    ScopedLockType lock (m_mutex);

    return m_minSeverity;
}

void LogSink::setMinSeverity (LogSeverity s, bool all)
{
    ScopedLockType lock (m_mutex);

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
    ScopedLockType lock (m_mutex);

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

void LogSink::format (
    std::string& output,
    std::string const& message,
    LogSeverity severity,
    std::string const& partitionName)
{
    output.reserve (message.size() + partitionName.size() + 100);

    output = boost::posix_time::to_simple_string (
        boost::posix_time::second_clock::universal_time ());

    output += " ";
    if (! partitionName.empty ())
        output += partitionName + ":";

    switch (severity)
    {
    case lsTRACE:       output += "TRC "; break;
    case lsDEBUG:       output += "DBG "; break;
    case lsINFO:        output += "NFO "; break;
    case lsWARNING:     output += "WRN "; break;
    case lsERROR:       output += "ERR "; break;
    default:
        bassertfalse;
    case lsFATAL:       output += "FTL ";
        break;
    }

    output += replaceFirstSecretWithAsterisks (message);

    if (output.size() > maximumMessageCharacters)
    {
        output.resize (maximumMessageCharacters - 3);
        output += "...";
    }
}

void LogSink::write (
    std::string const& message,
    LogSeverity severity,
    std::string const& partitionName)
{
    std::string output;

    format (output, message, severity, partitionName);

    write (output, severity);
}

void LogSink::write (std::string const& output, LogSeverity severity)
{
    ScopedLockType lock (m_mutex);
    write (output, severity >= getMinSeverity(), lock);
}

void LogSink::write (std::string const& text)
{
    ScopedLockType lock (m_mutex);
    write (text, true, lock);
}

void LogSink::write (std::string const& line, bool toStdErr, ScopedLockType&)
{
    // Does nothing if not open.
    m_logFile.writeln (line);

    if (toStdErr)
        std::cerr << line << std::endl;
}

void LogSink::write_console (std::string const& text)
{
#if BEAST_MSVC
    if (beast::beast_isRunningUnderDebugger ())
        beast::Logger::outputDebugString (text.c_str());
#endif
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
    return beast::SharedSingleton <LogSink>::getInstance ();
}

} // ripple
