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

#ifndef RIPPLE_CORE_JOB_H_INCLUDED
#define RIPPLE_CORE_JOB_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/core/ClosureCounter.h>
#include <ripple/core/LoadMonitor.h>
#include <functional>

#include <functional>

namespace ripple {

// Note that this queue should only be used for CPU-bound jobs
// It is primarily intended for signature checking

enum JobType {
    // Special type indicating an invalid job - will go away soon.
    jtINVALID = -1,

    // Job types - the position in this enum indicates the job priority with
    // earlier jobs having lower priority than later jobs. If you wish to
    // insert a job at a specific priority, simply add it at the right location.

    jtPACK,               // Make a fetch pack for a peer
    jtPUBOLDLEDGER,       // An old ledger has been accepted
    jtCLIENT,             // A placeholder for the priority of all jtCLIENT jobs
    jtCLIENT_SUBSCRIBE,   // A websocket subscription by a client
    jtCLIENT_FEE_CHANGE,  // Subscription for fee change by a client
    jtCLIENT_CONSENSUS,   // Subscription for consensus state change by a client
    jtCLIENT_ACCT_HIST,   // Subscription for account history by a client
    jtCLIENT_SHARD,       // Client request for shard archiving
    jtCLIENT_RPC,         // Client RPC request
    jtCLIENT_WEBSOCKET,   // Client websocket request
    jtRPC,                // A websocket command from the client
    jtSWEEP,              // Sweep for stale structures
    jtVALIDATION_ut,      // A validation from an untrusted source
    jtMANIFEST,           // A validator's manifest
    jtUPDATE_PF,          // Update pathfinding requests
    jtTRANSACTION_l,      // A local transaction
    jtREPLAY_REQ,         // Peer request a ledger delta or a skip list
    jtLEDGER_REQ,         // Peer request ledger/txnset data
    jtPROPOSAL_ut,        // A proposal from an untrusted source
    jtREPLAY_TASK,        // A Ledger replay task/subtask
    jtTRANSACTION,        // A transaction received from the network
    jtMISSING_TXN,        // Request missing transactions
    jtREQUESTED_TXN,      // Reply with requested transactions
    jtBATCH,              // Apply batched transactions
    jtLEDGER_DATA,        // Received data for a ledger we're acquiring
    jtADVANCE,            // Advance validated/acquired ledgers
    jtPUBLEDGER,          // Publish a fully-accepted ledger
    jtTXN_DATA,           // Fetch a proposed set
    jtWAL,                // Write-ahead logging
    jtVALIDATION_t,       // A validation from a trusted source
    jtWRITE,              // Write out hashed objects
    jtACCEPT,             // Accept a consensus ledger
    jtPROPOSAL_t,         // A proposal from a trusted source
    jtNETOP_CLUSTER,      // NetworkOPs cluster peer report
    jtNETOP_TIMER,        // NetworkOPs net timer processing
    jtADMIN,              // An administrative operation

    // Special job types which are not dispatched by the job pool
    jtPEER,
    jtDISK,
    jtTXN_PROC,
    jtOB_SETUP,
    jtPATH_FIND,
    jtHO_READ,
    jtHO_WRITE,
    jtGENERIC,  // Used just to measure time

    // Node store monitoring
    jtNS_SYNC_READ,
    jtNS_ASYNC_READ,
    jtNS_WRITE,
};

class Job : public CountedObject<Job>
{
public:
    using clock_type = std::chrono::steady_clock;

    /** Default constructor.

        Allows Job to be used as a container type.

        This is used to allow things like jobMap [key] = value.
    */
    // VFALCO NOTE I'd prefer not to have a default constructed object.
    //             What is the semantic meaning of a Job with no associated
    //             function? Having the invariant "all Job objects refer to
    //             a job" would reduce the number of states.
    //
    Job();

    Job(JobType type, std::uint64_t index);

    // VFALCO TODO try to remove the dependency on LoadMonitor.
    Job(JobType type,
        std::string const& name,
        std::uint64_t index,
        LoadMonitor& lm,
        std::function<void()> const& job);

    JobType
    getType() const;

    /** Returns the time when the job was queued. */
    clock_type::time_point const&
    queue_time() const;

    void
    doJob();

    // These comparison operators make the jobs sort in priority order
    // in the job set
    bool
    operator<(const Job& j) const;
    bool
    operator>(const Job& j) const;
    bool
    operator<=(const Job& j) const;
    bool
    operator>=(const Job& j) const;

private:
    JobType mType;
    std::uint64_t mJobIndex;
    std::function<void()> mJob;
    std::shared_ptr<LoadEvent> m_loadEvent;
    std::string mName;
    clock_type::time_point m_queue_time;
};

using JobCounter = ClosureCounter<void>;

}  // namespace ripple

#endif
