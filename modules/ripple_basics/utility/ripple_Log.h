//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_LOG_H
#define RIPPLE_LOG_H

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

// VFALCO TODO make this a nested class in Log
class LogPartition
{
protected:
    static LogPartition* headLog;

    LogPartition*       mNextLog;
    LogSeverity         mMinSeverity;
    std::string         mName;

public:
    LogPartition (const char* name);

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
    /** Retrieve file name from a log partition.
    */
    template <class Key>
    static char const* getFileName ();
    /*
    {
        static_vfassert (false);
    }
    */

public:
    template <class Key>
    static LogPartition const& get ()
    {
        static LogPartition logPartition (getFileName <Key> ());
        return logPartition;
    }
};

#define SETUP_LOG(k) \
    template <> char const* LogPartition::getFileName <k> () { return __FILE__; } \
    struct k##Instantiator { k##Instantiator () { LogPartition::get <k> (); } }; \
    static k##Instantiator k##Instantiator_instance;

//------------------------------------------------------------------------------

class Log
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

    static std::string severityToString (LogSeverity);

    static LogSeverity stringToSeverity (std::string const&);

    static LogSeverity getMinSeverity ();

    static void setMinSeverity (LogSeverity, bool all);

    static void setLogFile (boost::filesystem::path const& pathToLogFile);

    static std::string rotateLog ();

private:
    // VFALCO TODO derive from beast::Uncopyable
    Log (const Log&);            // no implementation
    Log& operator= (const Log&); // no implementation

    // VFALCO TODO looks like there are really TWO classes in here.
    //         One is a stream target for '<<' operator and the other
    //         is a singleton. Split the singleton out to a new class.
    //
    static boost::recursive_mutex sLock;
    static LogSeverity sMinSeverity;
    static std::ofstream* outStream;
    static boost::filesystem::path* pathToLog;
    static uint32 logRotateCounter;

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

// vim:ts=4
