/** @file
 *  Concrete implementation of the XRPL logging infrastructure.
 *
 *  Bridges the `beast::Journal` front-end abstraction with actual I/O:
 *  an append-mode log file and `stderr`.  Three collaborating classes are
 *  defined here — `Logs::Sink`, `Logs::File`, and the `Logs` coordinator —
 *  plus the file-local `DebugSink` that backs the global `debugLog()` journal.
 */
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
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

/** Construct a Sink for the named partition.
 *
 *  @param partition  Human-readable channel label (e.g. "Application").
 *  @param thresh     Initial severity threshold; messages below this level
 *      are discarded before formatting.
 *  @param logs       Owning `Logs` coordinator.  The reference must remain
 *      valid for the lifetime of this object, which is guaranteed because
 *      `Sink` instances are owned by `Logs::sinks_`.
 */
Logs::Sink::Sink(std::string partition, beast::Severity thresh, Logs& logs)
    : beast::Journal::Sink(thresh, false), logs_(logs), partition_(std::move(partition))
{
}

/** Write a message if it meets the per-sink severity threshold.
 *
 *  The threshold gate is applied here, close to where the formatted string
 *  was constructed, so callers that bypassed `beast::Journal::Stream`'s own
 *  active-check do not pay formatting cost for suppressed messages.
 *
 *  @param level  Severity of the message.
 *  @param text   Pre-formatted message text.
 */
void
Logs::Sink::write(beast::Severity level, std::string const& text)
{
    if (level < threshold())
        return;

    logs_.write(level, partition_, text, console());
}

/** Write a message unconditionally, bypassing the severity threshold.
 *
 *  Used for administrative override messages that must appear in the log
 *  regardless of the current verbosity configuration.
 *
 *  @param level  Severity attached to the message (used for formatting only).
 *  @param text   Pre-formatted message text.
 */
void
Logs::Sink::writeAlways(beast::Severity level, std::string const& text)
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

/** Construct the logging coordinator with a global severity threshold.
 *
 *  @param thresh  Initial threshold applied to every partition.  Individual
 *      partitions inherit this value and can be overridden later via
 *      `threshold(Severity)`.
 */
Logs::Logs(beast::Severity thresh) : thresh_(thresh)
{
}

/** Open a log file for append-mode writing.
 *
 *  @param pathToLogFile  Filesystem path of the log file.  Created if it
 *      does not exist; existing content is preserved.
 *  @return `true` if the file was successfully opened.
 */
bool
Logs::open(boost::filesystem::path const& pathToLogFile)
{
    return file_.open(pathToLogFile);
}

/** Return the sink for the named partition, creating it if necessary.
 *
 *  The `sinks_` map is keyed with case-insensitive comparison, so
 *  `"Application"` and `"application"` resolve to the same sink.
 *  `emplace()` is a no-op when the key already exists, so there is no
 *  risk of double-creating a sink under concurrent first-access.
 *
 *  @param name  Partition label.
 *  @return Reference to the sink; valid for the lifetime of this `Logs`.
 */
beast::Journal::Sink&
Logs::get(std::string const& name)
{
    std::scoped_lock const lock(mutex_);
    auto const result = sinks_.emplace(name, makeSink(name, thresh_));
    return *result.first->second;
}

/** Convenience overload; equivalent to `get(name)`.
 *
 *  @param name  Partition label.
 *  @return Reference to the sink for that partition.
 */
beast::Journal::Sink&
Logs::operator[](std::string const& name)
{
    return get(name);
}

/** Create a `beast::Journal` that writes to the named partition.
 *
 *  The returned journal is lightweight and copyable; subsystems typically
 *  store one as a member variable.
 *
 *  @param name  Partition label (case-insensitive).
 *  @return A `beast::Journal` backed by the partition's sink.
 */
beast::Journal
Logs::journal(std::string const& name)
{
    return beast::Journal(get(name));
}

/** Return the current global severity threshold.
 *
 *  @return The threshold that was most recently set via `threshold(Severity)`,
 *      or the value passed to the constructor if it was never changed.
 */
beast::Severity
Logs::threshold() const
{
    return thresh_;
}

/** Set the global severity threshold and propagate it to all existing sinks.
 *
 *  This is the mechanism behind the `logLevel` admin command on a running
 *  node: a single call fans out to every registered partition.  Sinks
 *  created after this call inherit the new threshold via `get()`.
 *
 *  @param thresh  New minimum severity; messages below this level are dropped.
 */
void
Logs::threshold(beast::Severity thresh)
{
    std::scoped_lock const lock(mutex_);
    thresh_ = thresh;
    for (auto& sink : sinks_)
        sink.second->threshold(thresh);
}

/** Snapshot the current severity level of every registered partition.
 *
 *  Takes the mutex even though the method is `const`, because it reads the
 *  live sink map that is mutated by `get()` and `threshold()`.
 *
 *  @return A vector of `{partitionName, severityString}` pairs, one per
 *      registered sink, in map iteration order.
 */
std::vector<std::pair<std::string, std::string>>
Logs::partitionSeverities() const
{
    std::vector<std::pair<std::string, std::string>> list;
    std::scoped_lock const lock(mutex_);
    list.reserve(sinks_.size());
    for (auto const& [name, sink] : sinks_)
        list.emplace_back(name, toString(sink->threshold()));
    return list;
}

/** Format and emit a log message to the file and optionally to stderr.
 *
 *  Formatting (timestamp, partition label, severity tag, length cap, and
 *  credential scrubbing) is performed before the mutex is taken so that
 *  the lock is held only for the actual I/O operations.
 *
 *  @param level      Severity of the message.
 *  @param partition  Partition label to include in the formatted output.
 *  @param text       Raw message text.
 *  @param console    Reserved for future console output; currently unused.
 */
void
Logs::write(
    beast::Severity level,
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

/** Close and reopen the log file to interoperate with log-rotation tools.
 *
 *  When an external tool (e.g. `logrotate(8)`) renames the active log file,
 *  a SIGHUP handler can call this method.  The file descriptor is released
 *  and then reopened at the original path, picking up the freshly created
 *  file.
 *
 *  @return A human-readable status string suitable for returning in an admin
 *      RPC response.
 */
std::string
Logs::rotate()
{
    std::scoped_lock const lock(mutex_);
    bool const wasOpened = file_.closeAndReopen();
    if (wasOpened)
        return "The log file was closed and reopened.";
    return "The log file could not be closed and reopened.";
}

/** Factory method for creating partition sinks; virtual for testability.
 *
 *  Test harnesses may subclass `Logs` and override this method to inject
 *  mock sinks without touching file I/O.
 *
 *  @param name       Partition label.
 *  @param threshold  Initial severity threshold for the new sink.
 *  @return Owning pointer to the new sink.
 */
std::unique_ptr<beast::Journal::Sink>
Logs::makeSink(std::string const& name, beast::Severity threshold)
{
    return std::make_unique<Sink>(name, threshold, *this);
}

/** Convert a `beast::Severity` value to a human-readable string.
 *
 *  @param s  The severity to convert.
 *  @return One of `"Trace"`, `"Debug"`, `"Info"`, `"Warning"`, `"Error"`,
 *      `"Fatal"`.  Fires `UNREACHABLE` in debug builds for unrecognised values.
 */
std::string
Logs::toString(beast::Severity s)
{
    switch (s)
    {
        case beast::Severity::Trace:
            return "Trace";
        case beast::Severity::Debug:
            return "Debug";
        case beast::Severity::Info:
            return "Info";
        case beast::Severity::Warning:
            return "Warning";
        case beast::Severity::Error:
            return "Error";
        case beast::Severity::Fatal:
            return "Fatal";
        // LCOV_EXCL_START
        default:
            UNREACHABLE("xrpl::Logs::toString : invalid severity");
            return "Unknown";
            // LCOV_EXCL_STOP
    }
}

/** Parse a severity level from a string, case-insensitively.
 *
 *  Accepts several alias spellings to be forgiving of operator input from
 *  config files or admin commands: `"warn"`, `"warnings"`, `"information"`,
 *  `"errors"`, `"fatals"`.
 *
 *  @param s  The string to parse.
 *  @return The corresponding `beast::Severity`, or `std::nullopt` if the
 *      string does not match any known level.
 */
std::optional<beast::Severity>
Logs::fromString(std::string const& s)
{
    if (boost::iequals(s, "trace"))
        return beast::Severity::Trace;

    if (boost::iequals(s, "debug"))
        return beast::Severity::Debug;

    if (boost::iequals(s, "info") || boost::iequals(s, "information"))
        return beast::Severity::Info;

    if (boost::iequals(s, "warn") || boost::iequals(s, "warning") || boost::iequals(s, "warnings"))
        return beast::Severity::Warning;

    if (boost::iequals(s, "error") || boost::iequals(s, "errors"))
        return beast::Severity::Error;

    if (boost::iequals(s, "fatal") || boost::iequals(s, "fatals"))
        return beast::Severity::Fatal;

    return std::nullopt;
}

/** Assemble a log line and scrub sensitive credential fields.
 *
 *  Produces a line of the form:
 *  @code
 *  2024-01-15T12:34:56Z Application:NFO some message text
 *  @endcode
 *
 *  After assembly, two post-processing steps are applied:
 *  1. **Length cap** — the total line is truncated to `kMAXIMUM_MESSAGE_CHARACTERS`
 *     (12 KB) and suffixed with `"..."` to indicate truncation.
 *  2. **Credential scrubbing** — the value following any of the JSON keys
 *     `"seed"`, `"seed_hex"`, `"secret"`, `"master_key"`, `"master_seed"`,
 *     `"master_seed_hex"`, or `"passphrase"` is replaced with asterisks.
 *     These are the exact field names used in XRPL's wallet and key-generation
 *     RPC calls (`wallet_propose`, `sign`, etc.), preventing accidental
 *     credential exposure if an RPC request body is logged verbatim.
 *
 *  @param output     Destination string; any prior content is overwritten.
 *  @param message    Raw message text to append after the header.
 *  @param severity   Determines the 3-letter tag (`TRC`, `DBG`, `NFO`,
 *      `WRN`, `ERR`, `FTL`).
 *  @param partition  Partition label; omitted from the header when empty.
 *  @note This method is static and does not take the mutex; callers are
 *      responsible for calling it before acquiring the lock if they need
 *      to minimise lock contention.
 */
void
Logs::format(
    std::string& output,
    std::string const& message,
    beast::Severity severity,
    std::string const& partition)
{
    output.reserve(message.size() + partition.size() + 100);

    output = xrpl::to_string(std::chrono::system_clock::now());

    output += " ";
    if (!partition.empty())
        output += partition + ":";

    using beast::Severity;
    switch (severity)
    {
        case Severity::Trace:
            output += "TRC ";
            break;
        case Severity::Debug:
            output += "DBG ";
            break;
        case Severity::Info:
            output += "NFO ";
            break;
        case Severity::Warning:
            output += "WRN ";
            break;
        case Severity::Error:
            output += "ERR ";
            break;
        // LCOV_EXCL_START
        default:
            UNREACHABLE("xrpl::Logs::format : invalid severity");
            [[fallthrough]];
        // LCOV_EXCL_STOP
        case Severity::Fatal:
            output += "FTL ";
            break;
    }

    output += message;

    if (output.size() > kMAXIMUM_MESSAGE_CHARACTERS)
    {
        output.resize(kMAXIMUM_MESSAGE_CHARACTERS - 3);
        output += "...";
    }

    auto scrubber = [&output](char const* token) {
        auto first = output.find(token);

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

/** Thread-safe holder for the process-wide debug journal sink.
 *
 *  Defaults to the null sink so that `debugLog()` is always safe to call
 *  even when no debug sink has been installed.  `set()` atomically swaps in
 *  a new sink and returns ownership of the previous one, enabling clean
 *  teardown in tests via `setDebugLogSink(nullptr)`.
 *
 *  @note Non-copyable and non-movable; intended for use as a function-local
 *      static only.
 */
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

    /** Atomically replace the active sink and return the previous one.
     *
     *  Passing `nullptr` resets the debug journal to the null sink.
     *
     *  @param sink  Owning pointer to the new sink, or `nullptr` to reset.
     *  @return Owning pointer to the sink that was active before this call.
     */
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

    /** Return a reference to the currently active sink.
     *
     *  @return The installed sink, or the null sink if none was installed.
     */
    beast::Journal::Sink&
    get()
    {
        std::scoped_lock const _(mtx_);
        return sink_.get();
    }
};

/** Return the process-wide `DebugSink` singleton. */
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
