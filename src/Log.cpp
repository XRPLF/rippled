
#include "Log.h"

#include <fstream>

#include <boost/date_time/posix_time/posix_time.hpp>

boost::recursive_mutex Log::sLock;

LogSeverity Log::sMinSeverity = lsINFO;

std::ofstream* Log::outStream = NULL;
boost::filesystem::path *Log::pathToLog = NULL;
uint32 Log::logRotateCounter = 0;

LogPartition* LogPartition::headLog = NULL;

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
		std::cerr << logMsg << std::endl;
	if (outStream != NULL)
		(*outStream) << logMsg << std::endl;
}


std::string Log::rotateLog(void) 
{
  boost::recursive_mutex::scoped_lock sl(sLock);
  boost::filesystem::path abs_path;
  std::string abs_path_str;

  uint32 failsafe = 0;

  std::string abs_new_path_str;
  do {
    std::string s;
    std::stringstream out;

    failsafe++;
    if (failsafe == std::numeric_limits<uint32>::max()) {
      return "unable to create new log file; too many log files!";
    }
    abs_path = boost::filesystem::absolute("");
    abs_path /=  *pathToLog;
    abs_path_str = abs_path.parent_path().string();
    out << logRotateCounter;
    s = out.str();


    abs_new_path_str = abs_path_str + "/" + s +  + "_" + pathToLog->filename().string();
  
    logRotateCounter++;

  } while (boost::filesystem::exists(boost::filesystem::path(abs_new_path_str)));

  outStream->close();
  boost::filesystem::rename(abs_path, boost::filesystem::path(abs_new_path_str));



  setLogFile(*pathToLog);

  return abs_new_path_str;
  
}

void Log::setMinSeverity(LogSeverity s)
{
	boost::recursive_mutex::scoped_lock sl(sLock);
	sMinSeverity = s;
	LogPartition::setSeverity(s);
}

void Log::setLogFile(boost::filesystem::path path)
{
	std::ofstream* newStream = new std::ofstream(path.c_str(), std::fstream::app);
	if (!newStream->good())
	{
		delete newStream;
		newStream = NULL;
	}

	boost::recursive_mutex::scoped_lock sl(sLock);
	if (outStream != NULL)
		delete outStream;
	outStream = newStream;
	if (outStream)
		Log(lsINFO) << "Starting up";

	pathToLog = new boost::filesystem::path(path);
}

void LogPartition::setSeverity(const char *partition, LogSeverity severity)
{
	for (LogPartition *p = headLog; p != NULL; p = p->mNextLog)
		if (p->mName == partition)
		{
			p->mMinSeverity = severity;
			return;
		}
}

void LogPartition::setSeverity(LogSeverity severity)
{
	for (LogPartition *p = headLog; p != NULL; p = p->mNextLog)
			p->mMinSeverity = severity;
}
