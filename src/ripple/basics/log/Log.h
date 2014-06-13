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

#ifndef RIPPLE_BASICS_LOG_H_INCLUDED
#define RIPPLE_BASICS_LOG_H_INCLUDED

#include <beast/utility/Journal.h>
#include <ripple/basics/log/LogSeverity.h>
#include <ripple/basics/log/LogSink.h>
#include <ripple/basics/log/LogPartition.h>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace ripple {

/** Manages partitions for logging. */
class Logs
{
private:
    class Sink : public beast::Journal::Sink
    {
    private:
        Logs& logs_;
        std::string partition_;

    public:
        Sink (std::string const& partition, Logs& logs)
            : logs_(logs)
            , partition_(partition)
        {
        }

        Sink (Sink const&) = delete;
        Sink& operator= (Sink const&) = delete;

        bool
        active (beast::Journal::Severity level) const override
        {
            return logs_.severity() <= level &&
                this->Sink::severity() <= level;
        }

        void
        write (beast::Journal::Severity level, std::string const& text)
        {
            logs_.write (level, partition_, text, console());
        }
    };

    std::mutex mutex_;
    std::unordered_map <std::string, Sink> sinks_;
    beast::Journal::Severity level_;
    LogSink out_;

public:
    Logs()
        : level_ (beast::Journal::kAll) // default severity
    {
    }

    Logs (Logs const&) = delete;
    Logs& operator= (Logs const&) = delete;
    
    void
    open (boost::filesystem::path const& pathToLogFile)
    {
        out_.setLogFile (pathToLogFile);
    }

    Sink&
    get (std::string const& name)
    {
        std::lock_guard <std::mutex> lock (mutex_);
        auto const result (sinks_.emplace(std::piecewise_construct,
            std::forward_as_tuple(name), std::forward_as_tuple(name, *this)));
        return result.first->second;
    }

    Sink&
    operator[] (std::string const& name)
    {
        return get(name);
    }

    beast::Journal
    journal (std::string const& name)
    {
        return beast::Journal (get(name));
    }

    void
    write (beast::Journal::Severity level, std::string const& partition,
        std::string const& text, bool console)
    {
        std::lock_guard <std::mutex> lock (mutex_);
        std::string s;
        out_.format (s, text, fromSeverity(level), partition);
        out_.write (s);
        if (console)
            out_.write_console(s);
    }

    beast::Journal::Severity
    severity() const
    {
        return level_;
    }

    void
    severity (beast::Journal::Severity level)
    {
        level_ = level;
    }

    static
    LogSeverity
    fromSeverity (beast::Journal::Severity level)
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

    static
    beast::Journal::Severity
    toSeverity (LogSeverity level)
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

    static
    std::string
    toString (LogSeverity s)
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

    static
    LogSeverity
    toSeverity (std::string const& s)
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
};

//------------------------------------------------------------------------------

/** RAII helper for writing to the LogSink. */
class Log : public beast::Uncopyable
{
public:
    explicit Log (LogSeverity s);
    Log (LogSeverity s, LogPartition& partition);
    ~Log ();

    template <class T>
    std::ostream& operator<< (const T& t) const
    {
        return m_os << t;
    }

    std::ostringstream& ref () const
    {
        return m_os;
    }

public:
    static std::string severityToString (LogSeverity);
    static LogSeverity stringToSeverity (std::string const&);

private:
    static std::string replaceFirstSecretWithAsterisks (std::string s);

    mutable std::ostringstream  m_os;
    LogSeverity                 m_level;
    LogPartition* m_partition;
};

//------------------------------------------------------------------------------
/*  DEPRECATED
    Inject beast::Journal instead
*/

// This is here temporarily until an interface is available to all clients
inline
Logs&
deprecatedLogs()
{
    static Logs logs;
    return logs;
}

#define ShouldLog(s, k) deprecatedLogs()[#k].active(Logs::toSeverity(s))

#define WriteLog(s, k) if (!ShouldLog (s, k)) do {} while (0); else \
    beast::Journal::Stream(deprecatedLogs()[#k], Logs::toSeverity(s))

#define CondLog(c, s, k) if (!ShouldLog (s, k) || !(c)) do {} while(0); else \
    beast::Journal::Stream(deprecatedLogs()[#k], Logs::toSeverity(s))

} // ripple

#endif
