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

#ifndef RIPPLE_BASICS_LOGSEVERITY_H_INCLUDED
#define RIPPLE_BASICS_LOGSEVERITY_H_INCLUDED

namespace ripple {

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

} // ripple

#endif
