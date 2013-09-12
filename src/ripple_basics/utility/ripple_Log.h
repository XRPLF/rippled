//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_LOG_H_INCLUDED
#define RIPPLE_LOG_H_INCLUDED

enum LogSeverity
{
    lsINVALID   = -1,   // used to indicate an invalid severity
    lsTRACE     = 0,    // Very low-level progress information, details inside an operation
    lsDEBUG     = 1,    // Function-level progress information, operations
    lsINFO      = 2,    // Server-level progress information, major operations
    lsWARNING   = 3,    // Conditions that warrant human attention, may indicate a problem
    lsERROR     = 4,    // A condition that indicates a problem
    lsFATAL     = 5     // A severe condition that indicates a server problem
};

//------------------------------------------------------------------------------

// VFALCO TODO make this a nested class in Log?
class LogPartition // : public List <LogPartition>::Node
{
public:
    LogPartition (const char* partitionName);

    /** Retrieve the LogPartition associated with an object.

        Each LogPartition is a singleton.
    */
    template <class Key>
    static LogPartition const& get ()
    {
        static LogPartition logPartition (getPartitionName <Key> ());
        return logPartition;
    }

    bool doLog (LogSeverity s) const
    {
        return s >= mMinSeverity;
    }

    const std::string& getName () const
    {
        return mName;
    }

    static bool setSeverity (const std::string& partition, LogSeverity severity);
    static void setSeverity (LogSeverity severity);
    static std::vector< std::pair<std::string, std::string> > getSeverities ();

private:
    /** Retrieve the name for a log partition.
    */
    template <class Key>
    static char const* getPartitionName ();

private:
    // VFALCO TODO Use an intrusive linked list
    //
    static LogPartition* headLog;

    LogPartition*       mNextLog;
    LogSeverity         mMinSeverity;
    std::string         mName;
};

#define SETUP_LOG(Class) \
    template <> char const* LogPartition::getPartitionName <Class> () { return #Class; } \
    struct Class##Instantiator { Class##Instantiator () { LogPartition::get <Class> (); } }; \
    static Class##Instantiator Class##Instantiator_instance;

#define SETUP_LOGN(Class,Name) \
    template <> char const* LogPartition::getPartitionName <Class> () { return Name; } \
    struct Class##Instantiator { Class##Instantiator () { LogPartition::get <Class> (); } }; \
    static Class##Instantiator Class##Instantiator_instance;

//------------------------------------------------------------------------------

// A singleton which performs the actual logging.
//
class LogInstance : public SharedSingleton <LogInstance>
{
public:
    LogInstance ();
    ~LogInstance ();
    static LogInstance* createInstance ();

    LogSeverity getMinSeverity ();

    void setMinSeverity (LogSeverity, bool all);

    void setLogFile (boost::filesystem::path const& pathToLogFile);

    /** Rotate the log file.

        The log file is closed and reopened. This is for compatibility
        with log management tools.

        @return A human readable string describing the result of the operation.
    */
    std::string rotateLog ();

    /** Write to log output.

        All logging eventually goes through this function. If a
        debugger is attached, the string goes to the debugging console,
        else it goes to the standard error output. If a log file is
        open, then the message is additionally written to the open log
        file.

        The text should not contain a newline, it will be automatically
        added as needed.

        @note  This acquires a global mutex.

        @param text     The text to write.
        @param toStdErr `true` to also write to std::cerr
    */
    void print (std::string const& text, bool toStdErr = true);

    void print (StringArray const& strings, bool toStdErr = true);

private:
    void write (std::string const& line, bool toStdErr);

    typedef RippleRecursiveMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;
    LockType m_mutex;

    LogFile m_logFile;
    LogSeverity m_minSeverity;
};

//------------------------------------------------------------------------------

// A RAII helper for writing to the LogInstance
//
class Log : public Uncopyable
{
public:
    explicit Log (LogSeverity s) : mSeverity (s)
    {
    }

    Log (LogSeverity s, LogPartition const& p)
        : mSeverity (s)
        , mPartitionName (p.getName ())
    {
    }

    ~Log ();

    template <class T>
    std::ostream& operator<< (const T& t) const
    {
        return oss << t;
    }

    std::ostringstream& ref () const
    {
        return oss;
    }

public:
    static std::string severityToString (LogSeverity);

    static LogSeverity stringToSeverity (std::string const&);

    /** Output stream for logging

        This is a convenient replacement for writing to `std::cerr`.

        Usage:

        @code

        Log::out () << "item1" << 2;

        @endcode

        It is not necessary to append a newline.
    */
    class out
    {
    public:
        out ()
        {
        }
        
        ~out ()
        {
            LogInstance::getInstance()->print (m_ss.str ());
        }

        template <class T>
        out& operator<< (T t)
        {
            m_ss << t;
            return *this;
        }

    private:
        std::stringstream m_ss;
    };

private:
    enum
    {
        /** Maximum line length for log messages.

            If the message exceeds this length it will be truncated
            with elipses.
        */
        maximumMessageCharacters = 12 * 1024
    };

    static std::string replaceFirstSecretWithAsterisks (std::string s);

    mutable std::ostringstream  oss;
    LogSeverity                 mSeverity;
    std::string                 mPartitionName;
};

// Manually test for whether we should log
//
#define ShouldLog(s, k) (LogPartition::get <k> ().doLog (s))

// Write to the log at the given severity level
//
#define WriteLog(s, k) if (!ShouldLog (s, k)) do {} while (0); else Log (s, LogPartition::get <k> ())

// Write to the log conditionally
//
#define CondLog(c, s, k) if (!ShouldLog (s, k) || !(c)) do {} while(0); else Log(s, LogPartition::get <k> ())

#endif
