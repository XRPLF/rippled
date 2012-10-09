#ifndef __LOG__
#define __LOG__

#include <sstream>
#include <string>
#include <limits>

#include <boost/thread/recursive_mutex.hpp>
#include <boost/filesystem.hpp>

// Ensure that we don't get value.h without writer.h
#include "../json/json.h"

#include "types.h"

// Put at the beginning of a C++ file that needs its own log partition
#define SETUP_LOG()	static LogPartition logPartition(__FILE__)

// Standard conditional log
#define cLog(x)		if (!logPartition.doLog(x)) do {} while (0); else Log(x)

// Log only if an additional condition 'c' is true. Condition is not computed if not needed
#define tLog(c,x)	if (!logPartition.doLog(x) || !(c)) do {} while(0); else Log(x)

// Check if should log
#define sLog(x)		(logPartition.doLog(x))


enum LogSeverity
{
	lsTRACE		= 0,	// Very low-level progress information, details inside an operation
	lsDEBUG		= 1,	// Function-level progress information, operations
	lsINFO		= 2,	// Server-level progress information, major operations
	lsWARNING	= 3,	// Conditions that warrant human attention, may indicate a problem
	lsERROR		= 4,	// A condition that indicates a problem
	lsFATAL		= 5		// A severe condition that indicates a server problem
};

class LogPartition
{
protected:
	static LogPartition* headLog;

	LogPartition*		mNextLog;
	LogSeverity			mMinSeverity;
	std::string			mName;

public:
	LogPartition(const char *name) : mNextLog(headLog), mMinSeverity(lsWARNING)
	{
		const char *ptr = strrchr(name, '/');
		mName = (ptr == NULL) ? name : ptr;
		headLog = this;
	}

	bool doLog(enum LogSeverity s)
	{
		return s >= mMinSeverity;
	}

	static void setSeverity(const char *partition, LogSeverity severity);
	static void setSeverity(LogSeverity severity);
};

class Log
{
private:
	Log(const Log&);			// no implementation
	Log& operator=(const Log&);	// no implementation

protected:
	static boost::recursive_mutex sLock;
	static LogSeverity sMinSeverity;
	static std::ofstream* outStream;

	mutable std::ostringstream oss;
	LogSeverity mSeverity;

	static boost::filesystem::path *pathToLog;
	static uint32 logRotateCounter;

public:
	Log(LogSeverity s) : mSeverity(s)
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

	static void setMinSeverity(LogSeverity);
	static void setLogFile(boost::filesystem::path);
	static std::string rotateLog(void);
};

#endif
