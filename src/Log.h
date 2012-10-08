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

#define SETUP_LOG()	static LogPartition logPartition(__FILE__)
#define cLog(x)		if (logPartition.doLog(x)) Log(x)

enum LogSeverity
{
	lsTRACE		= 0,
	lsDEBUG		= 1,
	lsINFO		= 2,
	lsWARNING	= 3,
	lsERROR		= 4,
	lsFATAL		= 5
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
