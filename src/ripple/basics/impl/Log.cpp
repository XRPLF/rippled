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

#include <BeastConfig.h>
#include <ripple/basics/Log.h>
#include <boost/algorithm/string.hpp>
// VFALCO TODO Use std::chrono
#include <boost/date_time/posix_time/posix_time.hpp>
#include <cassert>
#include <fstream>

namespace ripple {

Logs::Sink::Sink (std::string const& partition,
    beast::Journal::Severity severity, Logs& logs)
    : logs_(logs)
    , partition_(partition)
{
    beast::Journal::Sink::severity (severity);
}

void
Logs::Sink::write (beast::Journal::Severity level, std::string const& text)
{
    logs_.write (level, partition_, text, console());
}

//------------------------------------------------------------------------------

Logs::File::File()
    : m_stream (nullptr)
{
}

Logs::File::~File()
{
}

bool Logs::File::isOpen () const noexcept
{
    return m_stream != nullptr;
}

bool Logs::File::open (boost::filesystem::path const& path)
{
    close ();

    bool wasOpened = false;

    // VFALCO TODO Make this work with Unicode file paths
    std::unique_ptr <std::ofstream> stream (
        new std::ofstream (path.c_str (), std::fstream::app));

    if (stream->good ())
    {
        m_path = path;

        m_stream = std::move (stream);

        wasOpened = true;
    }

    return wasOpened;
}

bool Logs::File::closeAndReopen ()
{
    close ();

    return open (m_path);
}

void Logs::File::close ()
{
    m_stream = nullptr;
}

void Logs::File::write (char const* text)
{
    if (m_stream != nullptr)
        (*m_stream) << text;
}

void Logs::File::writeln (char const* text)
{
    if (m_stream != nullptr)
    {
        (*m_stream) << text;
        (*m_stream) << std::endl;
    }
}

//------------------------------------------------------------------------------

Logs::Logs()
    : level_ (beast::Journal::kWarning) // default severity
{
}

bool
Logs::open (boost::filesystem::path const& pathToLogFile)
{
    return file_.open(pathToLogFile);
}

Logs::Sink&
Logs::get (std::string const& name)
{
    std::lock_guard <std::mutex> lock (mutex_);
    auto const result (sinks_.emplace (std::piecewise_construct,
        std::forward_as_tuple(name), std::forward_as_tuple (name,
            level_, *this)));
    return result.first->second;
}

Logs::Sink&
Logs::operator[] (std::string const& name)
{
    return get(name);
}

beast::Journal
Logs::journal (std::string const& name)
{
    return beast::Journal (get(name));
}

beast::Journal::Severity
Logs::severity() const
{
    return level_;
}

void
Logs::severity (beast::Journal::Severity level)
{
    std::lock_guard <std::mutex> lock (mutex_);
    level_ = level;
    for (auto& sink : sinks_)
        sink.second.severity (level);
}

std::vector<std::pair<std::string, std::string>>
Logs::partition_severities() const
{
    std::vector<std::pair<std::string, std::string>> list;
    std::lock_guard <std::mutex> lock (mutex_);
    list.reserve (sinks_.size());
    for (auto const& e : sinks_)
        list.push_back(std::make_pair(e.first,
            toString(fromSeverity(e.second.severity()))));
    return list;
}

void
Logs::write (beast::Journal::Severity level, std::string const& partition,
    std::string const& text, bool console)
{
    std::string s;
    format (s, text, level, partition);
    std::lock_guard <std::mutex> lock (mutex_);
    file_.writeln (s);
    std::cerr << s << '\n';
    // VFALCO TODO Fix console output
    //if (console)
    //    out_.write_console(s);
}

std::string
Logs::rotate()
{
    std::lock_guard <std::mutex> lock (mutex_);
    bool const wasOpened = file_.closeAndReopen ();
    if (wasOpened)
        return "The log file was closed and reopened.";
    return "The log file could not be closed and reopened.";
}

LogSeverity
Logs::fromSeverity (beast::Journal::Severity level)
{
    using beast::Journal;
    switch (level)
    {
    case Journal::kTrace:   return lsTRACE;
    case Journal::kDebug:   return lsDEBUG;
    case Journal::kInfo:    return lsINFO;
    case Journal::kWarning: return lsWARNING;
    case Journal::kError:   return lsERROR;

    default:
        assert(false);
    case Journal::kFatal:
        break;
    }

    return lsFATAL;
}

beast::Journal::Severity
Logs::toSeverity (LogSeverity level)
{
    using beast::Journal;
    switch (level)
    {
    case lsTRACE:   return Journal::kTrace;
    case lsDEBUG:   return Journal::kDebug;
    case lsINFO:    return Journal::kInfo;
    case lsWARNING: return Journal::kWarning;
    case lsERROR:   return Journal::kError;
    default:
        assert(false);
    case lsFATAL:
        break;
    }

    return Journal::kFatal;
}

std::string
Logs::toString (LogSeverity s)
{
    switch (s)
    {
    case lsTRACE:   return "Trace";
    case lsDEBUG:   return "Debug";
    case lsINFO:    return "Info";
    case lsWARNING: return "Warning";
    case lsERROR:   return "Error";
    case lsFATAL:   return "Fatal";
    default:
        assert (false);
        return "Unknown";
    }
}

LogSeverity
Logs::fromString (std::string const& s)
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

// Replace the first secret, if any, with asterisks
std::string
Logs::scrub (std::string s)
{
    using namespace std;
    char const* secretToken = "\"secret\"";
    // Look for the first occurrence of "secret" in the string.
    size_t startingPosition = s.find (secretToken);
    if (startingPosition != string::npos)
    {
        // Found it, advance past the token.
        startingPosition += strlen (secretToken);
        // Replace the next 35 characters at most, without overwriting the end.
        size_t endingPosition = std::min (startingPosition + 35, s.size () - 1);
        for (size_t i = startingPosition; i < endingPosition; ++i)
            s [i] = '*';
    }
    return s;
}

void
Logs::format (std::string& output, std::string const& message,
    beast::Journal::Severity severity, std::string const& partition)
{
    output.reserve (message.size() + partition.size() + 100);

    output = boost::posix_time::to_simple_string (
        boost::posix_time::second_clock::universal_time ());

    output += " ";
    if (! partition.empty ())
        output += partition + ":";

    switch (severity)
    {
    case beast::Journal::kTrace:    output += "TRC "; break;
    case beast::Journal::kDebug:    output += "DBG "; break;
    case beast::Journal::kInfo:     output += "NFO "; break;
    case beast::Journal::kWarning:  output += "WRN "; break;
    case beast::Journal::kError:    output += "ERR "; break;
    default:
        assert(false);
    case beast::Journal::kFatal:    output += "FTL "; break;
    }

    output += scrub (message);

    if (output.size() > maximumMessageCharacters)
    {
        output.resize (maximumMessageCharacters - 3);
        output += "...";
    }
}

} // ripple
