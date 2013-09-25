//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_BASICS_LOGSINK_H_INCLUDED
#define RIPPLE_BASICS_LOGSINK_H_INCLUDED

/** An endpoint for all logging messages. */
class LogSink
{
public:
    LogSink ();
    ~LogSink ();

    /** Returns the minimum severity required for also writing to stderr. */
    LogSeverity getMinSeverity ();

    /** Sets the minimum severity required for also writing to stderr. */
    void setMinSeverity (LogSeverity, bool all);

    /** Sets the path to the log file. */
    void setLogFile (boost::filesystem::path const& pathToLogFile);

    /** Rotate the log file.
        The log file is closed and reopened. This is for compatibility
        with log management tools.
        @return A human readable string describing the result of the operation.
    */
    std::string rotateLog ();

    /** Write to log output.

        All logging eventually goes through this function. If a debugger
        is attached, the string goes to the debugging console, else it goes
        to the standard error output. If a log file is open, then the message
        is additionally written to the open log file.

        The text should not contain a newline, it will be automatically
        added as needed.

        @note  This acquires a global mutex.

        @param text     The text to write.
        @param toStdErr `true` to also write to std::cerr
    */
    /** @{ */
    void write (std::string const& message,
                LogSeverity severity, std::string const& partitionName);
    void write (std::string const& text);
    void write (StringArray const& strings);
    /** @} */

    /** Hides secret keys from log output. */
    static std::string replaceFirstSecretWithAsterisks (std::string s);

    /** Returns a pointer to the singleton. */
    typedef SharedPtr <SharedSingleton <LogSink> > Ptr;
    static Ptr get ();

private:
    typedef RippleRecursiveMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;

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
#endif
