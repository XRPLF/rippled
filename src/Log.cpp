
#include "Log.h"

#include <boost/date_time/posix_time/posix_time.hpp>

boost::recursive_mutex Log::sLock;

#ifdef DEBUG
LogSeverity Log::sMinSeverity = lsTRACE;
#else
LogSeverity Log::sMinSeverity = lsINFO;
#endif

Log::~Log()
{
	std::string logMsg = boost::posix_time::to_simple_string(boost::posix_time::second_clock::universal_time());
	switch (mSeverity)
	{
		case lsTRACE:	logMsg += " TRAC "; break;
		case lsDEBUG:	logMsg += " DEBG "; break;
		case lsINFO:	logMsg += " INFO "; break;
		case lsWARNING:	logMsg += " WARN "; break;
		case lsERROR:	logMsg += " EROR "; break;
		case lsFATAL:	logMsg += " FATL "; break;
	}
	logMsg += oss.str();
	boost::recursive_mutex::scoped_lock sl(sLock);
	if (mSeverity >= sMinSeverity)
	{
		std::cerr << logMsg << std::endl;
	}
}

void Log::setMinSeverity(LogSeverity s)
{
	boost::recursive_mutex::scoped_lock sl(sLock);
	sMinSeverity = s;
}
