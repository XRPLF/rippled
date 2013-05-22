#ifndef __LOG__
#define __LOG__

#include <sstream>
#include <string>
#include <limits>

#include <boost/thread/recursive_mutex.hpp>

// Forward declaration
namespace boost {
	namespace filesystem {
		class path;
	}
}

// Ensure that we don't get value.h without writer.h
#include "../json/json.h"

#include "types.h"

// DEPRECATED
// Put at the beginning of a C++ file that needs its own log partition
#define SETUP_LOG()	static LogPartition logPartition(__FILE__)
#define SETUP_NLOG(x) static LogPartition logPartition(x)

// DEPRECATED
// Standard conditional log
#define cLog(x)		if (!logPartition.doLog(x)) do {} while (0); else Log(x, logPartition)

// DEPRECATED
// Log only if an additional condition 'c' is true. Condition is not computed if not needed
#define tLog(c,x)	if (!logPartition.doLog(x) || !(c)) do {} while(0); else Log(x, logPartition)

// DEPRECATED
// Check if should log
#define sLog(x)		(logPartition.doLog(x))


enum LogSeverity
{
	lsINVALID	= -1,	// used to indicate an invalid severity
	lsTRACE		= 0,	// Very low-level progress information, details inside an operation
	lsDEBUG		= 1,	// Function-level progress information, operations
	lsINFO		= 2,	// Server-level progress information, major operations
	lsWARNING	= 3,	// Conditions that warrant human attention, may indicate a problem
	lsERROR		= 4,	// A condition that indicates a problem
	lsFATAL		= 5		// A severe condition that indicates a server problem
};

//------------------------------------------------------------------------------

// VFALCO: TODO, make this a nested class in Log
class LogPartition
{
protected:
	static LogPartition* headLog;

	LogPartition*		mNextLog;
	LogSeverity			mMinSeverity;
	std::string			mName;

public:
	LogPartition(const char *name);

	bool doLog(LogSeverity s) const	    { return s >= mMinSeverity;	}
	const std::string& getName() const	{ return mName; }

	static bool setSeverity(const std::string& partition, LogSeverity severity);
	static void setSeverity(LogSeverity severity);
	static std::vector< std::pair<std::string, std::string> > getSeverities();

private:
	template <class Key>
	inline static LogPartition getFileName ()
	{
		// VFALCO: TODO, to implement this correctly get __FILE__ from Key
		return __FILE__;
	}
public:
	template <class Key>
	inline static LogPartition const& get ()
	{
		static LogPartition logPartition (getFileName <Key> ());
		return logPartition;
	}
};

//------------------------------------------------------------------------------

class Log
{
private:
	Log(const Log&);			// no implementation
	Log& operator=(const Log&);	// no implementation

protected:
	static boost::recursive_mutex sLock;
	static LogSeverity sMinSeverity;
	static std::ofstream* outStream;

	mutable std::ostringstream	oss;
	LogSeverity					mSeverity;
	std::string					mPartitionName;

	static boost::filesystem::path *pathToLog;
	static uint32 logRotateCounter;

public:
	Log(LogSeverity s) : mSeverity(s)
	{ ; }

	Log(LogSeverity s, const LogPartition& p) : mSeverity(s), mPartitionName(p.getName())
	{ ; }

	~Log();

	template<typename T> std::ostream& operator<<(const T& t) const
	{
		return oss << t;
	}

	std::ostringstream& ref(void) const
	{
		return oss;
	}

	static std::string severityToString(LogSeverity);
	static LogSeverity stringToSeverity(const std::string&);

	static LogSeverity getMinSeverity();
	static void setMinSeverity(LogSeverity, bool all);
	static void setLogFile(boost::filesystem::path const&);
	static std::string rotateLog(void);
};

#define ShouldLog(s, k) (LogPartition::get <k> ().doLog (s))
#define WriteLog(s, k) if (!ShouldLog (s, k)) do {} while (0); else Log (s, LogPartition::get <k> ())
#define CondLog(c, s, k) if (!ShouldLog (s, k) || !(c)) do {} while(0); else Log(s, LogPartition::get <k> ())

#endif

// vim:ts=4
