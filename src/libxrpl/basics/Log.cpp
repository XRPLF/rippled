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

#include <xrpl/basics/Log.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/path.hpp>

#include <chrono>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ripple {

namespace {
constexpr auto FLUSH_INTERVAL =
    std::chrono::milliseconds(10);  // Max delay before flush
}


Logs::Sink::Sink(
    std::string const& partition,
    beast::severities::Severity thresh,
    Logs& logs)
    : beast::Journal::Sink(thresh, false), logs_(logs), partition_(partition)
{
}

void
Logs::Sink::write(beast::severities::Severity level, beast::Journal::StringBuffer text)
{
    if (level < threshold())
        return;

    logs_.write(level, partition_, text, console());
}

void
Logs::Sink::writeAlways(beast::severities::Severity level, beast::Journal::StringBuffer text)
{
    logs_.write(level, partition_, text, console());
}

//------------------------------------------------------------------------------

Logs::File::File() : m_stream(nullptr)
{
}

bool
Logs::File::isOpen() const noexcept
{
    return m_stream.has_value();
}

bool
Logs::File::open(boost::filesystem::path const& path)
{
    close();

    bool wasOpened = false;

    // VFALCO TODO Make this work with Unicode file paths
    std::ofstream stream(path.c_str(), std::fstream::app);

    if (stream.good())
    {
        m_path = path;

        m_stream = std::move(stream);
        size_t const bufsize = 256 * 1024;
        static char buf[bufsize];
        m_stream->rdbuf()->pubsetbuf(buf, bufsize);

        wasOpened = true;
    }

    return wasOpened;
}

bool
Logs::File::closeAndReopen()
{
    close();

    return open(m_path);
}

void
Logs::File::close()
{
    m_stream.reset();
}

void
Logs::File::write(std::string const& text)
{
    if (m_stream.has_value())
        m_stream->write(text.data(), text.size());
}

//------------------------------------------------------------------------------

Logs::Logs(beast::severities::Severity thresh)
    : thresh_(thresh)  // default severity
    , writeBuffer_(
          batchBuffer_)  // Initially, entire buffer is available for writing
    , readBuffer_(batchBuffer_.data(), 0)  // No data ready to flush initially
    , stopLogThread_(false)
{
    logThread_ = std::thread(&Logs::logThreadWorker, this);
}

Logs::~Logs()
{
    // Signal log thread to stop and wait for it to finish
    {
        stopLogThread_ = true;
    }
    
    if (logThread_.joinable())
        logThread_.join();
    
    flushBatch();  // Ensure all logs are written on shutdown
}

bool
Logs::open(boost::filesystem::path const& pathToLogFile)
{
    return file_.open(pathToLogFile);
}

beast::Journal::Sink&
Logs::get(std::string const& name)
{
    std::lock_guard lock(sinkSetMutex_);
    auto const result = sinks_.emplace(name, makeSink(name, thresh_));
    return *result.first->second;
}

beast::Journal::Sink&
Logs::operator[](std::string const& name)
{
    return get(name);
}

beast::severities::Severity
Logs::threshold() const
{
    return thresh_;
}

void
Logs::threshold(beast::severities::Severity thresh)
{
    std::lock_guard lock(sinkSetMutex_);
    thresh_ = thresh;
    for (auto& sink : sinks_)
        sink.second->threshold(thresh);
}

std::vector<std::pair<std::string, std::string>>
Logs::partition_severities() const
{
    std::vector<std::pair<std::string, std::string>> list;
    std::lock_guard lock(sinkSetMutex_);
    list.reserve(sinks_.size());
    for (auto const& [name, sink] : sinks_)
        list.emplace_back(name, toString(fromSeverity(sink->threshold())));
    return list;
}

void
Logs::write(
    beast::severities::Severity level,
    std::string const& partition,
    beast::Journal::StringBuffer text,
    bool console)
{
    std::string s;
    std::string_view result = text.str();
    if (!beast::Journal::isStructuredJournalEnabled())
    {
        format(s, text.str(), level, partition);
        text.str() = s;
        result = text.str();
    }

    // if (!silent_)
    //     std::cerr << result << '\n';

    messages_.push(text);
    
    // Signal log thread that new messages are available
    // logCondition_.notify_one();

    // Add to batch buffer for file output
    if (0) {
        // std::lock_guard lock(batchMutex_);

        // Console output still immediate for responsiveness
        // if (!silent_)
        //     std::cerr << result << '\n';

        size_t logSize = result.size() + 1;  // +1 for newline

        // If log won't fit in current write buffer, flush first
        if (logSize > writeBuffer_.size())
        {
            flushBatchUnsafe();
        }

        // Copy log into write buffer
        std::copy(result.begin(), result.end(), writeBuffer_.begin());
        writeBuffer_[result.size()] = '\n';

        return;

        // Update spans: expand read buffer, shrink write buffer
        size_t totalUsed = readBuffer_.size() + logSize;
        readBuffer_ = std::span<char>(batchBuffer_.data(), totalUsed);
        writeBuffer_ = std::span<char>(
            batchBuffer_.data() + totalUsed, batchBuffer_.size() - totalUsed);

        auto now = std::chrono::steady_clock::now();
        bool shouldFlush = (now - lastFlush_) >= FLUSH_INTERVAL;

        if (shouldFlush)
        {
            flushBatchUnsafe();
            lastFlush_ = now;
        }
    }

    // VFALCO TODO Fix console output
    // if (console)
    //    out_.write_console(s);
}

void
Logs::flushBatch()
{
    std::lock_guard lock(batchMutex_);
    flushBatchUnsafe();
}

void
Logs::flushBatchUnsafe()
{
    if (readBuffer_.empty())
        return;

    // Write the read buffer contents to file in one system call
    // file_.write(std::string_view{readBuffer_.data(), readBuffer_.size()});

    // Reset spans: entire buffer available for writing, nothing to read
    writeBuffer_ = std::span<char>(batchBuffer_);
    readBuffer_ = std::span<char>(batchBuffer_.data(), 0);
}

void
Logs::logThreadWorker()
{
    while (!stopLogThread_)
    {
        std::this_thread::sleep_for(FLUSH_INTERVAL);

        beast::Journal::StringBuffer buffer;
        // Process all available messages
        while (messages_.pop(buffer))
        {
            // Also write to console if not silent
            if (!silent_)
                std::cerr << buffer.str() << '\n';

            // Write to file
            file_.write(buffer.str());

            // Return node to pool for reuse
            beast::Journal::returnStringBuffer(std::move(buffer));
        }
    }
}

std::string
Logs::rotate()
{
    flushBatch();  // Flush pending logs before rotating
    bool const wasOpened = file_.closeAndReopen();
    if (wasOpened)
        return "The log file was closed and reopened.";
    return "The log file could not be closed and reopened.";
}

std::unique_ptr<beast::Journal::Sink>
Logs::makeSink(std::string const& name, beast::severities::Severity threshold)
{
    return std::make_unique<Sink>(name, threshold, *this);
}

LogSeverity
Logs::fromSeverity(beast::severities::Severity level)
{
    using namespace beast::severities;
    switch (level)
    {
        case kTrace:
            return lsTRACE;
        case kDebug:
            return lsDEBUG;
        case kInfo:
            return lsINFO;
        case kWarning:
            return lsWARNING;
        case kError:
            return lsERROR;

        default:
            UNREACHABLE("ripple::Logs::fromSeverity : invalid severity");
            [[fallthrough]];
        case kFatal:
            break;
    }

    return lsFATAL;
}

beast::severities::Severity
Logs::toSeverity(LogSeverity level)
{
    using namespace beast::severities;
    switch (level)
    {
        case lsTRACE:
            return kTrace;
        case lsDEBUG:
            return kDebug;
        case lsINFO:
            return kInfo;
        case lsWARNING:
            return kWarning;
        case lsERROR:
            return kError;
        default:
            UNREACHABLE("ripple::Logs::toSeverity : invalid severity");
            [[fallthrough]];
        case lsFATAL:
            break;
    }

    return kFatal;
}

std::string
Logs::toString(LogSeverity s)
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
            UNREACHABLE("ripple::Logs::toString : invalid severity");
            return "Unknown";
    }
}

LogSeverity
Logs::fromString(std::string const& s)
{
    if (boost::iequals(s, "trace"))
        return lsTRACE;

    if (boost::iequals(s, "debug"))
        return lsDEBUG;

    if (boost::iequals(s, "info") || boost::iequals(s, "information"))
        return lsINFO;

    if (boost::iequals(s, "warn") || boost::iequals(s, "warning") ||
        boost::iequals(s, "warnings"))
        return lsWARNING;

    if (boost::iequals(s, "error") || boost::iequals(s, "errors"))
        return lsERROR;

    if (boost::iequals(s, "fatal") || boost::iequals(s, "fatals"))
        return lsFATAL;

    return lsINVALID;
}

void
Logs::format(
    std::string& output,
    std::string_view message,
    beast::severities::Severity severity,
    std::string const& partition)
{
    output.reserve(message.size() + partition.size() + 100);

    output = to_string(std::chrono::system_clock::now());

    output += " ";
    if (!partition.empty())
        output += partition + ":";

    using namespace beast::severities;
    switch (severity)
    {
        case kTrace:
            output += "TRC ";
            break;
        case kDebug:
            output += "DBG ";
            break;
        case kInfo:
            output += "NFO ";
            break;
        case kWarning:
            output += "WRN ";
            break;
        case kError:
            output += "ERR ";
            break;
        default:
            UNREACHABLE("ripple::Logs::format : invalid severity");
            [[fallthrough]];
        case kFatal:
            output += "FTL ";
            break;
    }

    output += message;

    // Limit the maximum length of the output
    if (output.size() > maximumMessageCharacters)
    {
        output.resize(maximumMessageCharacters - 3);
        output += "...";
    }

    // Attempt to prevent sensitive information from appearing in log files by
    // redacting it with asterisks.
    auto scrubber = [&output](char const* token) {
        auto first = output.find(token);

        // If we have found the specified token, then attempt to isolate the
        // sensitive data (it's enclosed by double quotes) and mask it off:
        if (first != std::string::npos)
        {
            first = output.find('\"', first + std::strlen(token));

            if (first != std::string::npos)
            {
                auto last = output.find('\"', ++first);

                if (last == std::string::npos)
                    last = output.size();

                output.replace(first, last - first, last - first, '*');
            }
        }
    };

    scrubber("\"seed\"");
    scrubber("\"seed_hex\"");
    scrubber("\"secret\"");
    scrubber("\"master_key\"");
    scrubber("\"master_seed\"");
    scrubber("\"master_seed_hex\"");
    scrubber("\"passphrase\"");
}

//------------------------------------------------------------------------------

class DebugSink
{
private:
    std::reference_wrapper<beast::Journal::Sink> sink_;
    std::unique_ptr<beast::Journal::Sink> holder_;
    std::mutex m_;

public:
    DebugSink() : sink_(beast::Journal::getNullSink())
    {
    }

    DebugSink(DebugSink const&) = delete;
    DebugSink&
    operator=(DebugSink const&) = delete;

    DebugSink(DebugSink&&) = delete;
    DebugSink&
    operator=(DebugSink&&) = delete;

    std::unique_ptr<beast::Journal::Sink>
    set(std::unique_ptr<beast::Journal::Sink> sink)
    {
        std::lock_guard _(m_);

        using std::swap;
        swap(holder_, sink);

        if (holder_)
            sink_ = *holder_;
        else
            sink_ = beast::Journal::getNullSink();

        return sink;
    }

    beast::Journal::Sink&
    get()
    {
        std::lock_guard _(m_);
        return sink_.get();
    }
};

static DebugSink&
debugSink()
{
    static DebugSink _;
    return _;
}

std::unique_ptr<beast::Journal::Sink>
setDebugLogSink(std::unique_ptr<beast::Journal::Sink> sink)
{
    return debugSink().set(std::move(sink));
}

beast::Journal
debugLog()
{
    return beast::Journal(debugSink().get());
}

}  // namespace ripple
