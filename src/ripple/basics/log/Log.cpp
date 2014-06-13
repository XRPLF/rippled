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

namespace ripple {

Logs::Sink::Sink (std::string const& partition, Logs& logs)
    : logs_(logs)
    , partition_(partition)
{
}

bool
Logs::Sink::active (beast::Journal::Severity level) const
{
    return logs_.severity() <= level &&
        beast::Journal::Sink::severity() <= level;
}
        
beast::Journal::Severity
Logs::Sink::severity() const
{
    return beast::Journal::Sink::severity();
}

void
Logs::Sink::severity (beast::Journal::Severity level)
{
    std::lock_guard <std::mutex> lock (logs_.mutex_);
    beast::Journal::Sink::severity(level);
}

void
Logs::Sink::write (beast::Journal::Severity level, std::string const& text)
{
    logs_.write (level, partition_, text, console());
}

//------------------------------------------------------------------------------

Logs::Logs()
    : level_ (beast::Journal::kAll) // default severity
{
}
    
void
Logs::open (boost::filesystem::path const& pathToLogFile)
{
    out_.setLogFile (pathToLogFile);
}

Logs::Sink&
Logs::get (std::string const& name)
{
    std::lock_guard <std::mutex> lock (mutex_);
    auto const result (sinks_.emplace(std::piecewise_construct,
        std::forward_as_tuple(name), std::forward_as_tuple(name, *this)));
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
    // VFALCO Do we need the lock?
    level_ = level;
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
    std::lock_guard <std::mutex> lock (mutex_);
    std::string s;
    out_.format (s, text, fromSeverity(level), partition);
    out_.write (s);
    if (console)
        out_.write_console(s);
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
        bassertfalse;
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
        bassertfalse;
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

} // ripple
