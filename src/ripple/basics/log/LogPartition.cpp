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

#include <boost/algorithm/string.hpp>

namespace ripple {

void LogPartition::write (beast::Journal::Severity level, std::string const& text)
{
    std::string output;
    LogSeverity const logSeverity (convertSeverity (level));
    LogSink::get()->format (output, text, logSeverity, mName);
    LogSink::get()->write (output, logSeverity);
    if (console ())
        LogSink::get()->write_console (output);
}

//------------------------------------------------------------------------------

LogPartition::LogPartition (std::string const& name)
    : mNextLog (headLog)
    , mName (canonicalFileName (name.c_str()))
{
    severity (beast::Journal::kWarning);
    // VFALCO TODO Use an intrusive list.
    headLog = this;
}

bool LogPartition::doLog (LogSeverity s) const
{
    return s >= getSeverity();
}

std::string const& LogPartition::getName () const
{
    return mName;
}

LogSeverity LogPartition::getSeverity() const
{
    return convertSeverity (severity ());
}

void LogPartition::setMinimumSeverity (LogSeverity level)
{
    severity (convertLogSeverity (level));
}

//------------------------------------------------------------------------------

LogPartition* LogPartition::headLog = nullptr;

std::string LogPartition::canonicalFileName (char const* fileName)
{
    std::string result;
    char const* ptr (strrchr (fileName, '/'));
    result = (ptr == nullptr) ? fileName : (ptr + 1);

    size_t p = result.find (".cpp");

    if (p != std::string::npos)
        result.erase (result.begin () + p, result.end ());

    return result;
}

LogPartition* LogPartition::find (std::string const& name)
{
    for (LogPartition* p = headLog; p != nullptr; p = p->mNextLog)
        if (boost::iequals (p->getName(), name))
            return p;
    return nullptr;
}

void LogPartition::setSeverity (LogSeverity level)
{
    for (LogPartition* p = headLog; p != nullptr; p = p->mNextLog)
        p->setMinimumSeverity (level);
}

bool LogPartition::setSeverity (const std::string& partition, LogSeverity level)
{
    LogPartition* const p (find (partition));

    if (p)
    {
        p->setMinimumSeverity (level);
        return true;
    }

    return false;
}

void LogPartition::setConsoleOutput (std::string const& list)
{
    std::string::const_iterator first (list.begin());
    for(;;)
    {
        std::string::const_iterator last (std::find (
            first, list.end(), ','));

        LogPartition* const p (find (std::string (first, last)));
        if (p != nullptr)
            p->console (true);

        if (last != list.end())
            first = last + 1;
        else
            break;
    }
}

LogPartition::Severities LogPartition::getSeverities ()
{
    LogPartition::Severities result;

    for (LogPartition* l = headLog; l != nullptr; l = l->mNextLog)
        result.push_back (std::make_pair (
            l->mName, Log::severityToString (l->getSeverity ())));

    return result;
}

//------------------------------------------------------------------------------

LogSeverity LogPartition::convertSeverity (beast::Journal::Severity level)
{
    using beast::Journal;
    switch (level)
    {
    case Journal::kTrace:   return lsTRACE;
    case Journal::kDebug:   return lsDEBUG;
    case Journal::kInfo:    return lsINFO;
    case Journal::kWarning: return lsWARNING;
    case Journal::kError:   return lsERROR;

    default:
        bassertfalse;
    case Journal::kFatal:
        break;
    }

    return lsFATAL;
}

beast::Journal::Severity LogPartition::convertLogSeverity (LogSeverity level)
{
    using beast::Journal;
    switch (level)
    {
    case lsTRACE:   return Journal::kTrace;
    case lsDEBUG:   return Journal::kDebug;
    case lsINFO:    return Journal::kInfo;
    case lsWARNING: return Journal::kWarning;
    case lsERROR:   return Journal::kError;
    default:
        bassertfalse;
    case lsFATAL:
        break;
    }

    return Journal::kFatal;
}

} // ripple
