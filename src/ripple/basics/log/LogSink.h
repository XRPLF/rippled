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

#ifndef RIPPLE_BASICS_LOGSINK_H_INCLUDED
#define RIPPLE_BASICS_LOGSINK_H_INCLUDED

#include <ripple/basics/log/LogFile.h>
#include <beast/smart_ptr/SharedPtr.h>
#include <beast/module/core/memory/SharedSingleton.h>
#include <boost/filesystem.hpp>
#include <mutex>

namespace ripple {

/** An endpoint for all logging messages. */
class LogSink
{
public:
    LogSink ();
    ~LogSink ();

    /** Returns the minimum severity required for also writing to stderr. */
    LogSeverity getMinSeverity ();

    /** Sets the minimum severity required for also writing to stderr.
        If 'all' is true this will set the minimum reporting severity for
        all partitions.
    */
    void setMinSeverity (LogSeverity, bool all);

    /** Sets the path to the log file. */
    void setLogFile (boost::filesystem::path const& pathToLogFile);

    /** Rotate the log file.
        The log file is closed and reopened. This is for compatibility
        with log management tools.
        @return A human readable string describing the result of the operation.
    */
    std::string rotateLog ();

    /** Format a log message. */
    void format (std::string& output,
        std::string const& message, LogSeverity severity,
            std::string const& partitionName);

    /** Write to log output.
        All logging eventually goes through these functios.
        The text should not contain a final newline, it will be automatically
        added as needed.

        @note  This acquires a global mutex.

        @param text     The text to write.
        @param toStdErr `true` to also write to std::cerr
    */
    /** @{ */
    void write (std::string const& message, LogSeverity severity, std::string const& partitionName);
    void write (std::string const& text, LogSeverity severity);
    void write (std::string const& text);
    void write_console (std::string const& text);
    /** @} */

    /** Hides secret keys from log output. */
    static std::string replaceFirstSecretWithAsterisks (std::string s);

    /** Returns a pointer to the singleton. */
    typedef beast::SharedPtr <beast::SharedSingleton <LogSink> > Ptr;
    static Ptr get ();

private:
    typedef std::recursive_mutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;

    enum
    {
        /** Maximum line length for log messages.
            If the message exceeds this length it will be truncated with elipses.
        */
        maximumMessageCharacters = 12 * 1024
    };

    void write (std::string const& line, bool toStdErr, ScopedLockType&);

    LockType m_mutex;

    LogFile m_logFile;
    LogSeverity m_minSeverity;
};

} // ripple

#endif
