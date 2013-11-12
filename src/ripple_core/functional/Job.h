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

#ifndef RIPPLE_JOB_H
#define RIPPLE_JOB_H

// Note that this queue should only be used for CPU-bound jobs
// It is primarily intended for signature checking
enum JobType
{
    // must be in priority order, low to high
    jtINVALID       = -1,
    jtPACK          = 1,    // Make a fetch pack for a peer
    jtPUBOLDLEDGER  = 2,    // An old ledger has been accepted
    jtVALIDATION_ut = 3,    // A validation from an untrusted source
    jtPROOFWORK     = 4,    // A proof of work demand from another server
    jtTRANSACTION_l = 5,    // A local transaction
    jtPROPOSAL_ut   = 6,    // A proposal from an untrusted source
    jtLEDGER_DATA   = 7,    // Received data for a ledger we're acquiring
    jtUPDATE_PF     = 8,    // Update pathfinding requests
    jtCLIENT        = 9,    // A websocket command from the client
    jtRPC           = 10,    // A websocket command from the client
    jtTRANSACTION   = 11,   // A transaction received from the network
    jtUNL           = 12,   // A Score or Fetch of the UNL (DEPRECATED)
    jtADVANCE       = 13,   // Advance validated/acquired ledgers
    jtPUBLEDGER     = 14,   // Publish a fully-accepted ledger
    jtTXN_DATA      = 15,   // Fetch a proposed set
    jtWAL           = 16,   // Write-ahead logging
    jtVALIDATION_t  = 17,   // A validation from a trusted source
    jtWRITE         = 18,   // Write out hashed objects
    jtPROPOSAL_t    = 19,   // A proposal from a trusted source
    jtSWEEP         = 20,   // Sweep for stale structures
    jtNETOP_CLUSTER = 21,   // NetworkOPs cluster peer report
    jtNETOP_TIMER   = 22,   // NetworkOPs net timer processing
    jtADMIN         = 23,   // An administrative operation

    // special types not dispatched by the job pool
    jtPEER          = 30,
    jtDISK          = 31,
    jtACCEPTLEDGER  = 32,
    jtTXN_PROC      = 33,
    jtOB_SETUP      = 34,
    jtPATH_FIND     = 35,
    jtHO_READ       = 36,
    jtHO_WRITE      = 37,
    jtGENERIC       = 38,   // Used just to measure time
}; // CAUTION: If you add new types, add them to Job.cpp too

// VFALCO TODO move this into the enum so it calculates itself?
#define NUM_JOB_TYPES 48 // why 48 and not 38?

class Job
{
public:
    /** Default constructor.

        Allows Job to be used as a container type.

        This is used to allow things like jobMap [key] = value.
    */
    // VFALCO NOTE I'd prefer not to have a default constructed object.
    //             What is the semantic meaning of a Job with no associated
    //             function? Having the invariant "all Job objects refer to
    //             a job" would reduce the number of states.
    //
    Job ();

    Job (Job const& other);

    Job (JobType type, uint64 index);

    // VFALCO TODO try to remove the dependency on LoadMonitor.
    Job (JobType type,
         std::string const& name,
         uint64 index,
         LoadMonitor& lm,
         FUNCTION_TYPE <void (Job&)> const& job,
         CancelCallback cancelCallback);

    Job& operator= (Job const& other);

    JobType getType () const;

    CancelCallback getCancelCallback () const;

    /** Returns `true` if the running job should make a best-effort cancel. */
    bool shouldCancel () const;

    void doJob ();

    void rename (const std::string& n);

    // These comparison operators make the jobs sort in priority order in the job set
    bool operator< (const Job& j) const;
    bool operator> (const Job& j) const;
    bool operator<= (const Job& j) const;
    bool operator>= (const Job& j) const;

    static const char* toString (JobType);

private:
    CancelCallback m_cancelCallback;
    JobType                     mType;
    uint64                      mJobIndex;
    FUNCTION_TYPE <void (Job&)> mJob;
    LoadEvent::pointer          m_loadEvent;
    std::string                 mName;
};

#endif
