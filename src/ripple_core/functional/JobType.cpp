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
// Tracks every JobType that has been defined.
JobType::Map JobType::Jobs;

//==============================================================================
std::atomic <int> JobType::s_nextPriority (0);

//==============================================================================
JobType::JobType (char const* name_, int limit_, bool skip_)
    : priority (++s_nextPriority)
    , limit (limit_)
    , name (name_)
    , skip (skip_)
    , waiting (0)
    , running (0)
    , deferred (0)
{
    Jobs.insert (std::make_pair (
        limit, std::reference_wrapper<JobType> (*this)));
}

bool JobType::addTask ()
{
    ++waiting;

    if (waiting + running <= limit)
        return true;

    // We are over the limit so this task should be deferred until we go below
    ++deferred;

    return false;
}

//==============================================================================
bool operator< (JobType const& rhs, JobType const& lhs)
{
    return (rhs.priority < lhs.priority);
}

bool operator<= (JobType const& rhs, JobType const& lhs)
{
    return (rhs.priority <= lhs.priority);
}

bool operator> (JobType const& rhs, JobType const& lhs)
{
    return (rhs.priority > lhs.priority);
}

bool operator>= (JobType const& rhs, JobType const& lhs)
{
    return (rhs.priority >= lhs.priority);
}

bool operator== (JobType const& rhs, JobType const& lhs)
{
    return (rhs.priority == lhs.priority);
}

bool operator!= (JobType const& rhs, JobType const& lhs)
{
    return (rhs.priority != lhs.priority);
}

//==============================================================================
// These are all the job types that the server understands.

// NOTICE: It is *IMPORTANT* that jobs be declared in order of priority, from
//         low to high. Each will get assigned a strictly monotonically
//         increasing numerical priority.

// Make a fetch pack for a peer
JobType jtPACK ("makeFetchPack", 1, true);

// An old ledger has been accepted
JobType jtPUBOLDLEDGER ("publishAcqLedger", 2, true);

// A validation from an untrusted source
JobType jtVALIDATION_ut ("untrustedValidation", JobType::maxLimit, true);

// A proof of work demand from another server
JobType jtPROOFWORK ("proofOfWork", JobType::maxLimit, true);

// A local transaction
JobType jtTRANSACTION_l ("localTransaction", JobType::maxLimit, true);

// A proposal from an untrusted source
JobType jtPROPOSAL_ut ("untrustedProposal", JobType::maxLimit, true);

// Received data for a ledger we're acquiring
JobType jtLEDGER_DATA ("ledgerData", 2, true);

// Update pathfinding requests
JobType jtUPDATE_PF ("updatePaths", JobType::maxLimit, true);

// A websocket command from the client
JobType jtCLIENT ("clientCommand", JobType::maxLimit, true);

// A websocket command from the client
// don't skip (assert)
JobType jtRPC ("RPC", JobType::maxLimit, false);

// A transaction received from the network
JobType jtTRANSACTION ("transaction", JobType::maxLimit, true);

// A Score or Fetch of the UNL (DEPRECATED)
JobType jtUNL ("unl", 1, true);

// Advance validated/acquired ledgers
JobType jtADVANCE ("advanceLedger", JobType::maxLimit, true);

// Publish a fully-accepted ledger
JobType jtPUBLEDGER ("publishNewLedger", JobType::maxLimit, true);

// Fetch a proposed set
JobType jtTXN_DATA ("fetchTxnData", 1, true);

// Write-ahead logging
// don't skip
JobType jtWAL ("writeAhead", JobType::maxLimit, false);

// A validation from a trusted source
JobType jtVALIDATION_t ("trustedValidation", JobType::maxLimit, true);

// Write out hashed objects
// don't skip
JobType jtWRITE ("writeObjects", JobType::maxLimit, false);

// Accept a consensus ledger
// don't skip (assert)
JobType jtACCEPT ("acceptLedger", JobType::maxLimit, false);

// A proposal from a trusted source
JobType jtPROPOSAL_t ("trustedProposal", JobType::maxLimit, false);

// Sweep for stale structures
JobType jtSWEEP ("sweep", JobType::maxLimit, true);

// NetworkOPs cluster peer report
JobType jtNETOP_CLUSTER ("clusterReport", 1, true);

// NetworkOPs net timer processing
JobType jtNETOP_TIMER ("heartbeat", 1, true);

// An administrative operation
JobType jtADMIN ("administration", JobType::maxLimit, true);

// The rest are special job types that are not dispatched
// by the job pool. The "limit" and "skip" attributes are
// not applicable to these types of jobs.
JobType jtPEER ("peerCommand", 0, false);

JobType jtDISK ("diskAccess", 0, false);

JobType jtTXN_PROC ("processTransaction", 0, false);

JobType jtOB_SETUP ("orderBookSetup", 0, false);

JobType jtPATH_FIND ("pathFind", 0, false);

JobType jtHO_READ ("nodeRead", 0, false);

JobType jtHO_WRITE ("nodeWrite", 0, false);

JobType jtGENERIC ("generic", 0, false);
