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

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/beast/utility/Journal.h>

#include <boost/beast/core/string.hpp>
#include <boost/filesystem.hpp>

#include <boost/lockfree/queue.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <utility>

namespace ripple {

// DEPRECATED use beast::severities::Severity instead
enum LogSeverity {
    lsINVALID = -1,  // used to indicate an invalid severity
    lsTRACE = 0,     // Very low-level progress information, details inside
                     // an operation
    lsDEBUG = 1,     // Function-level progress information, operations
    lsINFO = 2,      // Server-level progress information, major operations
    lsWARNING = 3,   // Conditions that warrant human attention, may indicate
                     // a problem
    lsERROR = 4,     // A condition that indicates a problem
    lsFATAL = 5      // A severe condition that indicates a server problem
};

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
        Sink(
            std::string const& partition,
            beast::severities::Severity thresh,
            Logs& logs);

        Sink(Sink const&) = delete;
        Sink&
        operator=(Sink const&) = delete;

        void
        write(beast::severities::Severity level, beast::Journal::StringBuffer text) override;

        void
        writeAlways(beast::severities::Severity level, beast::Journal::StringBuffer text)
            override;
    };

    /** Manages a system file containing logged output.
        The system file remains open during program execution. Interfaces
        are provided for interoperating with standard log management
        tools like logrotate(8):
            http://linuxcommand.org/man_pages/logrotate8.html
        @note None of the listed interfaces are thread-safe.
    */
    class File
    {
    public:
        /** Construct with no associated system file.
            A system file may be associated later with @ref open.
            @see open
        */
        File();

        /** Destroy the object.
            If a system file is associated, it will be flushed and closed.
        */
        ~File() = default;

        /** Determine if a system file is associated with the log.
            @return `true` if a system file is associated and opened for
            writing.
        */
        bool
        isOpen() const noexcept;

        /** Associate a system file with the log.
            If the file does not exist an attempt is made to create it
            and open it for writing. If the file already exists an attempt is
            made to open it for appending.
            If a system file is already associated with the log, it is closed
            first.
            @return `true` if the file was opened.
        */
        bool
        open(boost::filesystem::path const& path);

        /** Close and re-open the system file associated with the log
            This assists in interoperating with external log management tools.
            @return `true` if the file was opened.
        */
        bool
        closeAndReopen();

        /** Close the system file if it is open. */
        void
        close();

        /** write to the log file.
            Does nothing if there is no associated system file.
        */
        void
        write(std::string const& str);

        /** @} */

    private:
        std::optional<std::ofstream> m_stream;
        boost::filesystem::path m_path;
    };

    std::mutex mutable sinkSetMutex_;
    std::map<
        std::string,
        std::unique_ptr<beast::Journal::Sink>,
        boost::beast::iless>
        sinks_;
    beast::severities::Severity thresh_;
    File file_;
    bool silent_ = false;

    // Batching members
    mutable std::mutex batchMutex_;
    boost::lockfree::queue<beast::Journal::StringBuffer, boost::lockfree::capacity<100>> messages_;
    static constexpr size_t BATCH_BUFFER_SIZE = 64 * 1024;  // 64KB buffer
    std::array<char, BATCH_BUFFER_SIZE> batchBuffer_{};
    std::span<char> writeBuffer_;  // Points to available write space
    std::span<char> readBuffer_;   // Points to data ready to flush

    // Log thread members
    std::thread logThread_;
    std::atomic<bool> stopLogThread_;
    std::mutex logMutex_;
    std::condition_variable logCondition_;
    
private:
    std::chrono::steady_clock::time_point lastFlush_ =
        std::chrono::steady_clock::now();

public:
    Logs(beast::severities::Severity level);

    Logs(Logs const&) = delete;
    Logs&
    operator=(Logs const&) = delete;

    virtual ~Logs();  // Need to flush on destruction

    bool
    open(boost::filesystem::path const& pathToLogFile);

    beast::Journal::Sink&
    get(std::string const& name);

    beast::Journal::Sink&
    operator[](std::string const& name);

    template <typename AttributesFactory>
    beast::Journal
    journal(std::string const& name, AttributesFactory&& factory)
    {
        return beast::Journal{
            get(name), name, std::forward<AttributesFactory>(factory)};
    }

    beast::Journal
    journal(std::string const& name)
    {
        return beast::Journal{get(name), name};
    }

    beast::severities::Severity
    threshold() const;

    void
    threshold(beast::severities::Severity thresh);

    std::vector<std::pair<std::string, std::string>>
    partition_severities() const;

    void
    write(
        beast::severities::Severity level,
        std::string const& partition,
        beast::Journal::StringBuffer text,
        bool console);

    std::string
    rotate();

    void
    flushBatch();

    /**
     * Set flag to write logs to stderr (false) or not (true).
     *
     * @param bSilent Set flag accordingly.
     */
    void
    silent(bool bSilent)
    {
        silent_ = bSilent;
    }

    virtual std::unique_ptr<beast::Journal::Sink>
    makeSink(
        std::string const& partition,
        beast::severities::Severity startingLevel);

public:
    static LogSeverity
    fromSeverity(beast::severities::Severity level);

    static beast::severities::Severity
    toSeverity(LogSeverity level);

    static std::string
    toString(LogSeverity s);

    static LogSeverity
    fromString(std::string const& s);

    static void
    format(
        std::string& output,
        std::string_view message,
        beast::severities::Severity severity,
        std::string const& partition);

private:
    enum {
        // Maximum line length for log messages.
        // If the message exceeds this length it will be truncated with elipses.
        maximumMessageCharacters = 12 * 1024
    };

    void
    flushBatchUnsafe();
    
    void
    logThreadWorker();
};

// Wraps a Journal::Stream to skip evaluation of
// expensive argument lists if the stream is not active.
#ifndef JLOG
#define JLOG_JOIN_(a,b) a##b
#define JLOG_JOIN(a,b)  JLOG_JOIN_(a,b)
#define JLOG_UNIQUE(base) JLOG_JOIN(base, __LINE__)   // line-based unique name

#define JLOG(x) \
    if (auto JLOG_UNIQUE(stream) = (x); !JLOG_UNIQUE(stream)) \
    { \
    } \
    else \
        JLOG_UNIQUE(stream)
#endif

#ifndef CLOG
#define CLOG(ss) \
    if (!ss)     \
        ;        \
    else         \
        *ss
#endif

//------------------------------------------------------------------------------
// Debug logging:

/** Set the sink for the debug journal.

    @param sink unique_ptr to new debug Sink.
    @return unique_ptr to the previous Sink.  nullptr if there was no Sink.
*/
std::unique_ptr<beast::Journal::Sink>
setDebugLogSink(std::unique_ptr<beast::Journal::Sink> sink);

/** Returns a debug journal.
    The journal may drain to a null sink, so its output
    may never be seen. Never use it for critical
    information.
*/
beast::Journal
debugLog();

}  // namespace ripple

#endif
