//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
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
    jtTRANSACTION   = 10,   // A transaction received from the network
    jtUNL           = 11,   // A Score or Fetch of the UNL (DEPRECATED)
    jtADVANCE       = 12,   // Advance validated/acquired ledgers
    jtPUBLEDGER     = 13,   // Publish a fully-accepted ledger
    jtTXN_DATA      = 14,   // Fetch a proposed set
    jtWAL           = 15,   // Write-ahead logging
    jtVALIDATION_t  = 16,   // A validation from a trusted source
    jtWRITE         = 17,   // Write out hashed objects
    jtPROPOSAL_t    = 18,   // A proposal from a trusted source
    jtSWEEP         = 19,   // Sweep for stale structures
    jtNETOP_CLUSTER = 20,   // NetworkOPs cluster peer report
    jtNETOP_TIMER   = 21,   // NetworkOPs net timer processing
    jtADMIN         = 22,   // An administrative operation

    // special types not dispatched by the job pool
    jtPEER          = 24,
    jtDISK          = 25,
    jtACCEPTLEDGER  = 26,
    jtTXN_PROC      = 27,
    jtOB_SETUP      = 28,
    jtPATH_FIND     = 29,
    jtHO_READ       = 30,
    jtHO_WRITE      = 31,
    jtGENERIC       = 32,   // Used just to measure time
}; // CAUTION: If you add new types, add them to ripple_Job.cpp too

// VFALCO TODO move this into the enum so it calculates itself?
#define NUM_JOB_TYPES 48 // why 48 and not 32?

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

    Job (JobType type, uint64 index);

    // VFALCO TODO try to remove the dependency on LoadMonitor.
    Job (JobType type,
         std::string const& name,
         uint64 index,
         LoadMonitor& lm,
         FUNCTION_TYPE <void (Job&)> const& job);

    JobType getType () const;

    void doJob ();

    void rename (const std::string& n);

    LoadEvent& peekEvent() const;

    // These comparison operators make the jobs sort in priority order in the job set
    bool operator< (const Job& j) const;
    bool operator> (const Job& j) const;
    bool operator<= (const Job& j) const;
    bool operator>= (const Job& j) const;

    static const char* toString (JobType);

private:
    JobType                     mType;
    uint64                      mJobIndex;
    FUNCTION_TYPE <void (Job&)> mJob;
    LoadEvent::pointer          m_loadEvent;
    std::string                 mName;
};

#endif
