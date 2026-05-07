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

namespace xrpl {

Logs::Sink::Sink(std::string partition, beast::severities::Severity thresh, Logs& logs)
    : beast::Journal::Sink(thresh, false), logs_(logs), partition_(std::move(partition))
{
}

void
Logs::Sink::write(beast::severities::Severity level, std::string const& text)
{
    if (level < threshold())
        return;

    logs_.write(level, partition_, text, console());
}

void
Logs::Sink::writeAlways(beast::severities::Severity level, std::string const& text)
{
    logs_.write(level, partition_, text, console());
}

//------------------------------------------------------------------------------

Logs::File::File() : stream_(nullptr)
{
}

bool
Logs::File::isOpen() const noexcept
{
    return stream_ != nullptr;
}

bool
Logs::File::open(boost::filesystem::path const& path)
{
    close();

    bool wasOpened = false;

    // VFALCO TODO Make this work with Unicode file paths
    std::unique_ptr<std::ofstream> stream(new std::ofstream(path.c_str(), std::fstream::app));

    if (stream->good())
    {
        path_ = path;

        stream_ = std::move(stream);

        wasOpened = true;
    }

    return wasOpened;
}

bool
Logs::File::closeAndReopen()
{
    close();

    return open(path_);
}

void
Logs::File::close()
{
    stream_ = nullptr;
}

void
Logs::File::write(char const* text)
{
    if (stream_ != nullptr)
        (*stream_) << text;
}

void
Logs::File::writeln(char const* text)
{
    if (stream_ != nullptr)
    {
        (*stream_) << text;
        (*stream_) << std::endl;
    }
}

//------------------------------------------------------------------------------

Logs::Logs(beast::severities::Severity thresh) : thresh_(thresh)  // default severity
{
}

bool
Logs::open(boost::filesystem::path const& pathToLogFile)
{
    return file_.open(pathToLogFile);
}

beast::Journal::Sink&
Logs::get(std::string const& name)
{
    std::scoped_lock const lock(mutex_);
    auto const result = sinks_.emplace(name, makeSink(name, thresh_));
    return *result.first->second;
}

beast::Journal::Sink&
Logs::operator[](std::string const& name)
{
    return get(name);
}

beast::Journal
Logs::journal(std::string const& name)
{
    return beast::Journal(get(name));
}

beast::severities::Severity
Logs::threshold() const
{
    return thresh_;
}

void
Logs::threshold(beast::severities::Severity thresh)
{
    std::scoped_lock const lock(mutex_);
    thresh_ = thresh;
    for (auto& sink : sinks_)
        sink.second->threshold(thresh);
}

std::vector<std::pair<std::string, std::string>>
Logs::partitionSeverities() const
{
    std::vector<std::pair<std::string, std::string>> list;
    std::scoped_lock const lock(mutex_);
    list.reserve(sinks_.size());
    for (auto const& [name, sink] : sinks_)
        list.emplace_back(name, toString(fromSeverity(sink->threshold())));
    return list;
}

void
Logs::write(
    beast::severities::Severity level,
    std::string const& partition,
    std::string const& text,
    bool console)
{
    std::string s;
    format(s, text, level, partition);
    std::scoped_lock const lock(mutex_);
    file_.writeln(s);
    if (!silent_)
        std::cerr << s << '\n';
    // VFALCO TODO Fix console output
    // if (console)
    //    out_.write_console(s);
}

std::string
Logs::rotate()
{
    std::scoped_lock const lock(mutex_);
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
        case KTrace:
            return LSTrace;
        case KDebug:
            return LSDebug;
        case KInfo:
            return LSInfo;
        case KWarning:
            return LSWarning;
        case KError:
            return LSError;

        // LCOV_EXCL_START
        default:
            UNREACHABLE("xrpl::Logs::fromSeverity : invalid severity");
            [[fallthrough]];
        // LCOV_EXCL_STOP
        case KFatal:
            break;
    }

    return LSFatal;
}

beast::severities::Severity
Logs::toSeverity(LogSeverity level)
{
    using namespace beast::severities;
    switch (level)
    {
        case LSTrace:
            return KTrace;
        case LSDebug:
            return KDebug;
        case LSInfo:
            return KInfo;
        case LSWarning:
            return KWarning;
        case LSError:
            return KError;
        // LCOV_EXCL_START
        default:
            UNREACHABLE("xrpl::Logs::toSeverity : invalid severity");
            [[fallthrough]];
        // LCOV_EXCL_STOP
        case LSFatal:
            break;
    }

    return KFatal;
}

std::string
Logs::toString(LogSeverity s)
{
    switch (s)
    {
        case LSTrace:
            return "Trace";
        case LSDebug:
            return "Debug";
        case LSInfo:
            return "Info";
        case LSWarning:
            return "Warning";
        case LSError:
            return "Error";
        case LSFatal:
            return "Fatal";
        // LCOV_EXCL_START
        default:
            UNREACHABLE("xrpl::Logs::toString : invalid severity");
            return "Unknown";
            // LCOV_EXCL_STOP
    }
}

LogSeverity
Logs::fromString(std::string const& s)
{
    if (boost::iequals(s, "trace"))
        return LSTrace;

    if (boost::iequals(s, "debug"))
        return LSDebug;

    if (boost::iequals(s, "info") || boost::iequals(s, "information"))
        return LSInfo;

    if (boost::iequals(s, "warn") || boost::iequals(s, "warning") || boost::iequals(s, "warnings"))
        return LSWarning;

    if (boost::iequals(s, "error") || boost::iequals(s, "errors"))
        return LSError;

    if (boost::iequals(s, "fatal") || boost::iequals(s, "fatals"))
        return LSFatal;

    return LSInvalid;
}

void
Logs::format(
    std::string& output,
    std::string const& message,
    beast::severities::Severity severity,
    std::string const& partition)
{
    output.reserve(message.size() + partition.size() + 100);

    output = xrpl::to_string(std::chrono::system_clock::now());

    output += " ";
    if (!partition.empty())
        output += partition + ":";

    using namespace beast::severities;
    switch (severity)
    {
        case KTrace:
            output += "TRC ";
            break;
        case KDebug:
            output += "DBG ";
            break;
        case KInfo:
            output += "NFO ";
            break;
        case KWarning:
            output += "WRN ";
            break;
        case KError:
            output += "ERR ";
            break;
        // LCOV_EXCL_START
        default:
            UNREACHABLE("xrpl::Logs::format : invalid severity");
            [[fallthrough]];
        // LCOV_EXCL_STOP
        case KFatal:
            output += "FTL ";
            break;
    }

    output += message;

    // Limit the maximum length of the output
    if (output.size() > MaximumMessageCharacters)
    {
        output.resize(MaximumMessageCharacters - 3);
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
    std::mutex mtx_;

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
        std::scoped_lock const _(mtx_);

        using std::swap;
        swap(holder_, sink);

        if (holder_)
        {
            sink_ = *holder_;
        }
        else
        {
            sink_ = beast::Journal::getNullSink();
        }

        return sink;
    }

    beast::Journal::Sink&
    get()
    {
        std::scoped_lock const _(mtx_);
        return sink_.get();
    }
};

static DebugSink&
debugSink()
{
    static DebugSink kINST;
    return kINST;
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

}  // namespace xrpl
