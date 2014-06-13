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
        File ();

        /** Destroy the object.
            If a system file is associated, it will be flushed and closed.
        */
        ~File ();

        /** Determine if a system file is associated with the log.
            @return `true` if a system file is associated and opened for writing.
        */
        bool isOpen () const noexcept;

        /** Associate a system file with the log.
            If the file does not exist an attempt is made to create it
            and open it for writing. If the file already exists an attempt is
            made to open it for appending.
            If a system file is already associated with the log, it is closed first.
            @return `true` if the file was opened.
        */
        // VFALCO NOTE The parameter is unfortunately a boost type because it
        //             can be either wchar or char based depending on platform.
        //        TODO Replace with beast::File
        //
        bool open (boost::filesystem::path const& path);

        /** Close and re-open the system file associated with the log
            This assists in interoperating with external log management tools.
            @return `true` if the file was opened.
        */
        bool closeAndReopen ();

        /** Close the system file if it is open. */
        void close ();

        /** write to the log file.
            Does nothing if there is no associated system file.
        */
        void write (char const* text);

        /** write to the log file and append an end of line marker.
            Does nothing if there is no associated system file.
        */
        void writeln (char const* text);

        /** Write to the log file using std::string. */
        /** @{ */
        void write (std::string const& str)
        {
            write (str.c_str ());
        }
        
        void writeln (std::string const& str)
        {
            writeln (str.c_str ());
        }
        /** @} */

    private:
        std::unique_ptr <std::ofstream> m_stream;
        boost::filesystem::path m_path;
    };

    std::mutex mutable mutex_;
    std::unordered_map <std::string, Sink> sinks_;
    beast::Journal::Severity level_;
    File file_;

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

private:
    enum
    {
        // Maximum line length for log messages.
        // If the message exceeds this length it will be truncated with elipses.
        maximumMessageCharacters = 12 * 1024
    };

    static
    std::string
    scrub (std::string s);

    static
    void
    format (std::string& output, std::string const& message,
        beast::Journal::Severity severity, std::string const& partition);
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
