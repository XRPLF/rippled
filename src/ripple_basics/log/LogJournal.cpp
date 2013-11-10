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

LogJournal::PartitionSinkBase::PartitionSinkBase (LogPartition& partition)
    : m_partition (partition)
    , m_severity (Journal::kLowestSeverity)
    , m_to_console (false)
{
#ifdef RIPPLE_JOURNAL_MSVC_OUTPUT
    std::string const& list (RIPPLE_JOURNAL_MSVC_OUTPUT);

    std::string::const_iterator first (list.begin());
    for(;;)
    {
        std::string::const_iterator last (std::find (
            first, list.end(), ','));

        if (std::equal (first, last, m_partition.getName().begin()))
        {
            set_console (true);
        #ifdef RIPPLE_JOURNAL_MSVC_OUTPUT_SEVERITY
            set_severity (RIPPLE_JOURNAL_MSVC_OUTPUT_SEVERITY);
        #endif
            break;
        }

        if (last != list.end())
            first = last + 1;
        else
            break;
    }
#endif
}

void LogJournal::PartitionSinkBase::write (
    Journal::Severity severity, std::string const& text)
{
    std::string output;
    LogSeverity const logSeverity (convertSeverity (severity));
    LogSink::get()->format (output, text, logSeverity,
        m_partition.getName());
    LogSink::get()->write (output, logSeverity);
#if BEAST_MSVC
    if (m_to_console && beast_isRunningUnderDebugger ())
        Logger::outputDebugString (output.c_str());
#endif
}

bool LogJournal::PartitionSinkBase::active (
    Journal::Severity severity)
{
    return m_partition.doLog (convertSeverity (severity));
}

bool LogJournal::PartitionSinkBase::console()
{
    return m_to_console;
}

void LogJournal::PartitionSinkBase::set_severity (
    Journal::Severity severity)
{
    LogSeverity const logSeverity (convertSeverity (severity));
    m_partition.setMinimumSeverity (logSeverity);
}

void LogJournal::PartitionSinkBase::set_console (bool to_console)
{
    m_to_console = to_console;
}

//------------------------------------------------------------------------------

LogSeverity LogJournal::convertSeverity (Journal::Severity severity)
{
    switch (severity)
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
