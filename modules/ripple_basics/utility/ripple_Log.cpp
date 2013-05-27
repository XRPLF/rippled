
boost::recursive_mutex Log::sLock;

LogSeverity Log::sMinSeverity = lsINFO;

std::ofstream* Log::outStream = NULL;
boost::filesystem::path *Log::pathToLog = NULL;
uint32 Log::logRotateCounter = 0;

#ifndef LOG_MAX_MESSAGE
#define LOG_MAX_MESSAGE (12 * 1024)
#endif

LogPartition* LogPartition::headLog = NULL;

LogPartition::LogPartition(const char *name) : mNextLog(headLog), mMinSeverity(lsWARNING)
{
	const char *ptr = strrchr(name, '/');
	mName = (ptr == NULL) ? name : (ptr + 1);

	size_t p = mName.find(".cpp");
	if (p != std::string::npos)
		mName.erase(mName.begin() + p, mName.end());

	headLog = this;
}

std::vector< std::pair<std::string, std::string> > LogPartition::getSeverities()
{
	std::vector< std::pair<std::string, std::string> > sevs;

	for (LogPartition *l = headLog; l != NULL; l = l->mNextLog)
		sevs.push_back(std::make_pair(l->mName, Log::severityToString(l->mMinSeverity)));

	return sevs;
}

Log::~Log()
{
	std::string logMsg = boost::posix_time::to_simple_string(boost::posix_time::second_clock::universal_time());
	if (!mPartitionName.empty())
		logMsg += " " + mPartitionName + ":";
	else
		logMsg += " ";

	switch (mSeverity)
	{
		case lsTRACE:	logMsg += "TRC "; break;
		case lsDEBUG:	logMsg += "DBG "; break;
		case lsINFO:	logMsg += "NFO "; break;
		case lsWARNING:	logMsg += "WRN "; break;
		case lsERROR:	logMsg += "ERR "; break;
		case lsFATAL:	logMsg += "FTL "; break;
		case lsINVALID:	assert(false); return;
	}

	logMsg += oss.str();
	if (logMsg.size() > LOG_MAX_MESSAGE)
	{
		logMsg.resize(LOG_MAX_MESSAGE);
		logMsg += "...";
	}

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
    abs_path		= boost::filesystem::absolute("");
    abs_path		/= *pathToLog;
    abs_path_str	= abs_path.parent_path().string();

    out << logRotateCounter;
    s = out.str();

    abs_new_path_str = abs_path_str + "/" + s + "_" + pathToLog->filename().string();

    logRotateCounter++;

  } while (boost::filesystem::exists(boost::filesystem::path(abs_new_path_str)));

  outStream->close();

  try
  {
    boost::filesystem::rename(abs_path, boost::filesystem::path(abs_new_path_str));
  }
  catch (...)
  {
    // unable to rename existing log file
  }

  setLogFile(*pathToLog);

  return abs_new_path_str;
}

void Log::setMinSeverity(LogSeverity s, bool all)
{
	boost::recursive_mutex::scoped_lock sl(sLock);

	sMinSeverity = s;
	if (all)
		LogPartition::setSeverity(s);
}

LogSeverity Log::getMinSeverity()
{
	boost::recursive_mutex::scoped_lock sl(sLock);

	return sMinSeverity;
}

std::string Log::severityToString(LogSeverity s)
{
	switch (s)
	{
		case lsTRACE:	return "Trace";
		case lsDEBUG:	return "Debug";
		case lsINFO:	return "Info";
		case lsWARNING: return "Warning";
		case lsERROR:	return "Error";
		case lsFATAL:	return "Fatal";
		default:		assert(false); return "Unknown";
	}
}

LogSeverity Log::stringToSeverity(const std::string& s)
{
	if (boost::iequals(s, "trace"))
		return lsTRACE;
	if (boost::iequals(s, "debug"))
		return lsDEBUG;
	if (boost::iequals(s, "info") || boost::iequals(s, "information"))
		return lsINFO;
	if (boost::iequals(s, "warn") || boost::iequals(s, "warning") || boost::iequals(s, "warnings"))
		return lsWARNING;
	if (boost::iequals(s, "error") || boost::iequals(s, "errors"))
		return lsERROR;
	if (boost::iequals(s, "fatal") || boost::iequals(s, "fatals"))
		return lsFATAL;
	return lsINVALID;
}

void Log::setLogFile(boost::filesystem::path const& path)
{
	std::ofstream* newStream = new std::ofstream(path.c_str(), std::fstream::app);
	if (!newStream->good())
	{
		Log(lsFATAL) << "Unable to open logfile " << path;
		delete newStream;
		newStream = NULL;
	}

	boost::recursive_mutex::scoped_lock sl(sLock);
	if (outStream != NULL)
		delete outStream;
	outStream = newStream;

	if (pathToLog != NULL)
		delete pathToLog;
	pathToLog = new boost::filesystem::path(path);
}

bool LogPartition::setSeverity(const std::string& partition, LogSeverity severity)
{
	for (LogPartition *p = headLog; p != NULL; p = p->mNextLog)
		if (boost::iequals(p->mName, partition))
		{
			p->mMinSeverity = severity;
			return true;
		}
	return false;
}

void LogPartition::setSeverity(LogSeverity severity)
{
	for (LogPartition *p = headLog; p != NULL; p = p->mNextLog)
			p->mMinSeverity = severity;
}

// vim:ts=4
