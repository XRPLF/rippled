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
#include <ripple/basics/chrono.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/contract.h>
#include <boost/algorithm/string.hpp>
#include <cassert>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>

namespace ripple {

Logs::Sink::Sink (std::string const& partition,
    beast::severities::Severity thresh, Logs& logs)
    : beast::Journal::Sink (thresh, false)
    , logs_(logs)
    , partition_(partition)
{
}

void
Logs::Sink::write (beast::severities::Severity level, std::string const& text)
{
    if (level < threshold())
        return;

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

Logs::Logs(beast::severities::Severity thresh)
    : thresh_ (thresh) // default severity
{
}

bool
Logs::open (boost::filesystem::path const& pathToLogFile)
{
    return file_.open(pathToLogFile);
}

beast::Journal::Sink&
Logs::get (std::string const& name)
{
    std::lock_guard <std::mutex> lock (mutex_);
    auto const result =
        sinks_.emplace(name, makeSink(name, thresh_));
    return *result.first->second;
}

beast::Journal::Sink&
Logs::operator[] (std::string const& name)
{
    return get(name);
}

beast::Journal
Logs::journal (std::string const& name)
{
    return beast::Journal (get(name));
}

beast::severities::Severity
Logs::threshold() const
{
    return thresh_;
}

void
Logs::threshold (beast::severities::Severity thresh)
{
    std::lock_guard <std::mutex> lock (mutex_);
    thresh_ = thresh;
    for (auto& sink : sinks_)
        sink.second->threshold (thresh);
}

std::vector<std::pair<std::string, std::string>>
Logs::partition_severities() const
{
    std::vector<std::pair<std::string, std::string>> list;
    std::lock_guard <std::mutex> lock (mutex_);
    list.reserve (sinks_.size());
    for (auto const& e : sinks_)
        list.push_back(std::make_pair(e.first,
            toString(fromSeverity(e.second->threshold()))));
    return list;
}

void
Logs::write (beast::severities::Severity level, std::string const& partition,
    std::string const& text, bool console)
{
    std::string s;
    format (s, text, level, partition);
    std::lock_guard <std::mutex> lock (mutex_);
    file_.writeln (s);
    if (! silent_)
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

std::unique_ptr<beast::Journal::Sink>
Logs::makeSink(std::string const& name,
    beast::severities::Severity threshold)
{
    return std::make_unique<Sink>(
        name, threshold, *this);
}

LogSeverity
Logs::fromSeverity (beast::severities::Severity level)
{
    using namespace beast::severities;
    switch (level)
    {
    case kTrace:   return lsTRACE;
    case kDebug:   return lsDEBUG;
    case kInfo:    return lsINFO;
    case kWarning: return lsWARNING;
    case kError:   return lsERROR;

    default:
        assert(false);
    case kFatal:
        break;
    }

    return lsFATAL;
}

beast::severities::Severity
Logs::toSeverity (LogSeverity level)
{
    using namespace beast::severities;
    switch (level)
    {
    case lsTRACE:   return kTrace;
    case lsDEBUG:   return kDebug;
    case lsINFO:    return kInfo;
    case lsWARNING: return kWarning;
    case lsERROR:   return kError;
    default:
        assert(false);
    case lsFATAL:
        break;
    }

    return kFatal;
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

void
Logs::format (std::string& output, std::string const& message,
    beast::severities::Severity severity, std::string const& partition)
{
    output.reserve (message.size() + partition.size() + 100);

    output = to_string(std::chrono::system_clock::now());

    output += " ";
    if (! partition.empty ())
        output += partition + ":";

    using namespace beast::severities;
    switch (severity)
    {
    case kTrace:    output += "TRC "; break;
    case kDebug:    output += "DBG "; break;
    case kInfo:     output += "NFO "; break;
    case kWarning:  output += "WRN "; break;
    case kError:    output += "ERR "; break;
    default:
        assert(false);
    case kFatal:    output += "FTL "; break;
    }

    output += message;

    // Limit the maximum length of the output
    if (output.size() > maximumMessageCharacters)
    {
        output.resize (maximumMessageCharacters - 3);
        output += "...";
    }

    // Attempt to prevent sensitive information from appearing in log files by
    // redacting it with asterisks.
    auto scrubber = [&output](char const* token)
    {
        auto first = output.find(token);

        // If we have found the specified token, then attempt to isolate the
        // sensitive data (it's enclosed by double quotes) and mask it off:
        if (first != std::string::npos)
        {
            first = output.find ('\"', first + std::strlen(token));

            if (first != std::string::npos)
            {
                auto last = output.find('\"', ++first);

                if (last == std::string::npos)
                    last = output.size();

                output.replace (first, last - first, last - first, '*');
            }
        }
    };

    scrubber ("\"seed\"");
    scrubber ("\"seed_hex\"");
    scrubber ("\"secret\"");
    scrubber ("\"master_key\"");
    scrubber ("\"master_seed\"");
    scrubber ("\"master_seed_hex\"");
    scrubber ("\"passphrase\"");
}

//------------------------------------------------------------------------------

class DebugSink
{
private:
    std::reference_wrapper<beast::Journal::Sink> sink_;
    std::unique_ptr<beast::Journal::Sink> holder_;
    std::mutex m_;

public:
    DebugSink ()
        : sink_ (beast::Journal::getNullSink())
    {
    }

    DebugSink (DebugSink const&) = delete;
    DebugSink& operator=(DebugSink const&) = delete;

    DebugSink(DebugSink&&) = delete;
    DebugSink& operator=(DebugSink&&) = delete;

    std::unique_ptr<beast::Journal::Sink>
    set(std::unique_ptr<beast::Journal::Sink> sink)
    {
        std::lock_guard<std::mutex> _(m_);

        using std::swap;
        swap (holder_, sink);

        if (holder_)
            sink_ = *holder_;
        else
            sink_ = beast::Journal::getNullSink();

        return sink;
    }

    beast::Journal::Sink&
    get()
    {
        std::lock_guard<std::mutex> _(m_);
        return sink_.get();
    }
};

static
DebugSink&
debugSink()
{
    static DebugSink _;
    return _;
}

std::unique_ptr<beast::Journal::Sink>
setDebugLogSink(
    std::unique_ptr<beast::Journal::Sink> sink)
{
    return debugSink().set(std::move(sink));
}

beast::Journal
debugLog()
{
    return beast::Journal (debugSink().get());
}

} // ripple
