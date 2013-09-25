//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_BASICS_LOGSEVERITY_H_INCLUDED
#define RIPPLE_BASICS_LOGSEVERITY_H_INCLUDED

enum LogSeverity
{
    lsINVALID   = -1,   // used to indicate an invalid severity
    lsTRACE     = 0,    // Very low-level progress information, details inside an operation
    lsDEBUG     = 1,    // Function-level progress information, operations
    lsINFO      = 2,    // Server-level progress information, major operations
    lsWARNING   = 3,    // Conditions that warrant human attention, may indicate a problem
    lsERROR     = 4,    // A condition that indicates a problem
    lsFATAL     = 5     // A severe condition that indicates a server problem
};

#endif
