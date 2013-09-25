//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

LogPartition* LogPartition::headLog = NULL;

LogPartition::LogPartition (char const* partitionName)
    : mNextLog (headLog)
    , mMinSeverity (lsWARNING)
{
    const char* ptr = strrchr (partitionName, '/');
    mName = (ptr == NULL) ? partitionName : (ptr + 1);

    size_t p = mName.find (".cpp");

    if (p != std::string::npos)
        mName.erase (mName.begin () + p, mName.end ());

    headLog = this;
}

std::vector< std::pair<std::string, std::string> > LogPartition::getSeverities ()
{
    std::vector< std::pair<std::string, std::string> > sevs;

    for (LogPartition* l = headLog; l != NULL; l = l->mNextLog)
        sevs.push_back (std::make_pair (l->mName, Log::severityToString (l->mMinSeverity)));

    return sevs;
}

//------------------------------------------------------------------------------

bool LogPartition::setSeverity (const std::string& partition, LogSeverity severity)
{
    for (LogPartition* p = headLog; p != NULL; p = p->mNextLog)
        if (boost::iequals (p->mName, partition))
        {
            p->mMinSeverity = severity;
            return true;
        }

    return false;
}

void LogPartition::setSeverity (LogSeverity severity)
{
    for (LogPartition* p = headLog; p != NULL; p = p->mNextLog)
        p->mMinSeverity = severity;
}
