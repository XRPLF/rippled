//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
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

void LogPartition::setMinimumSeverity (LogSeverity severity)
{
    mMinSeverity = severity;
}

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
