//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

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
