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
        Sink (std::string const& partition, Logs& logs);

        Sink (Sink const&) = delete;
        Sink& operator= (Sink const&) = delete;

        bool
        active (beast::Journal::Severity level) const override;
        
        beast::Journal::Severity
        severity() const override;

        void
        severity (beast::Journal::Severity level) override;

        void
        write (beast::Journal::Severity level, std::string const& text) override;
    };

    std::mutex mutable mutex_;
    std::unordered_map <std::string, Sink> sinks_;
    beast::Journal::Severity level_;
    LogSink out_;

public:
    Logs();

    Logs (Logs const&) = delete;
    Logs& operator= (Logs const&) = delete;
    
    void
    open (boost::filesystem::path const& pathToLogFile);

    Sink&
    get (std::string const& name);

    Sink&
    operator[] (std::string const& name);

    beast::Journal
    journal (std::string const& name);

    beast::Journal::Severity
    severity() const;

    void
    severity (beast::Journal::Severity level);

    std::vector<std::pair<std::string, std::string>>
    partition_severities() const;

    void
    write (beast::Journal::Severity level, std::string const& partition,
        std::string const& text, bool console);

    std::string
    rotate();

public:
    static
    LogSeverity
    fromSeverity (beast::Journal::Severity level);

    static
    beast::Journal::Severity
    toSeverity (LogSeverity level);

    static
    std::string
    toString (LogSeverity s);

    static
    LogSeverity
    fromString (std::string const& s);
};

//------------------------------------------------------------------------------
// VFALCO DEPRECATED Temporary transition function until interfaces injected
inline
Logs&
deprecatedLogs()
{
    static Logs logs;
    return logs;
}
// VFALCO DEPRECATED Inject beast::Journal instead
#define ShouldLog(s, k) deprecatedLogs()[#k].active(Logs::toSeverity(s))
#define WriteLog(s, k) if (!ShouldLog (s, k)) do {} while (0); else \
    beast::Journal::Stream(deprecatedLogs()[#k], Logs::toSeverity(s))
#define CondLog(c, s, k) if (!ShouldLog (s, k) || !(c)) do {} while(0); else \
    beast::Journal::Stream(deprecatedLogs()[#k], Logs::toSeverity(s))

} // ripple

#endif
