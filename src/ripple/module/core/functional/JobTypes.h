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

#ifndef RIPPLE_JOBTYPES_H_INCLUDED
#define RIPPLE_JOBTYPES_H_INCLUDED

#include <ripple/module/core/functional/Job.h>
#include <ripple/module/core/functional/JobTypeInfo.h>
#include <map>

namespace ripple
{

class JobTypes
{
public:
    typedef std::map <JobType, JobTypeInfo> Map;
    typedef Map::const_iterator const_iterator;


    JobTypes ()
        : m_unknown (jtINVALID, "invalid", 0, true, true, 0, 0)
    {        
        int maxLimit = std::numeric_limits <int>::max ();

        // Make a fetch pack for a peer
        add (jtPACK,          "makeFetchPack",
            1,        true,   false, 0,     0);

        // An old ledger has been accepted
        add (jtPUBOLDLEDGER,  "publishAcqLedger",
            2,        true,   false, 10000, 15000);

        // A validation from an untrusted source
        add (jtVALIDATION_ut, "untrustedValidation",
            maxLimit, true,   false, 2000,  5000);

        // A proof of work demand from another server
        add (jtPROOFWORK,     "proofOfWork",
            maxLimit, true,   false, 2000,  5000);

        // A local transaction
        add (jtTRANSACTION_l, "localTransaction",
            maxLimit, true,   false, 100,   500);

        // A proposal from an untrusted source
        add (jtPROPOSAL_ut,   "untrustedProposal",
            maxLimit, true,   false, 500,   1250);

        // Received data for a ledger we're acquiring
        add (jtLEDGER_DATA,   "ledgerData",
            2,        true,   false, 0,     0);

        // Update pathfinding requests
        add (jtUPDATE_PF,     "updatePaths",
            maxLimit, true,   false, 0,     0);

        // A websocket command from the client
        add (jtCLIENT,        "clientCommand",
            maxLimit, true,   false, 2000,  5000);

        // A websocket command from the client
        add (jtRPC,           "RPC",
            maxLimit, false,  false, 0,     0);

        // A transaction received from the network
        add (jtTRANSACTION,   "transaction",
            maxLimit, true,   false, 250,   1000);

        // A Score or Fetch of the UNL (DEPRECATED)
        add (jtUNL,           "unl",
            1,        true,   false, 0,     0);

        // Advance validated/acquired ledgers
        add (jtADVANCE,       "advanceLedger",
            maxLimit, true,   false, 0,     0);

        // Publish a fully-accepted ledger
        add (jtPUBLEDGER,     "publishNewLedger",
            maxLimit, true,   false, 3000,  4500);

        // Fetch a proposed set
        add (jtTXN_DATA,      "fetchTxnData",
            1,        true,   false, 0,     0);

        // Write-ahead logging
        add (jtWAL,           "writeAhead",
            maxLimit, false,  false, 1000,  2500);

        // A validation from a trusted source
        add (jtVALIDATION_t,  "trustedValidation",
            maxLimit, true,   false, 500,  1500);

        // Write out hashed objects
        add (jtWRITE,         "writeObjects",
            maxLimit, false,  false, 1750,  2500);

        // Accept a consensus ledger
        add (jtACCEPT,        "acceptLedger",
            maxLimit, false,  false, 0,     0);

        // A proposal from a trusted source
        add (jtPROPOSAL_t,    "trustedProposal",
            maxLimit, false,  false, 100,   500);

        // Sweep for stale structures
        add (jtSWEEP,         "sweep",
            maxLimit, true,   false, 0,     0);

        // NetworkOPs cluster peer report
        add (jtNETOP_CLUSTER, "clusterReport",
            1,        true,   false, 9999,  9999);

        // NetworkOPs net timer processing
        add (jtNETOP_TIMER,   "heartbeat",
            1,        true,   false, 999,   999);

        // An administrative operation
        add (jtADMIN,         "administration",
            maxLimit, true,   false, 0,     0);

        // The rest are special job types that are not dispatched
        // by the job pool. The "limit" and "skip" attributes are
        // not applicable to these types of jobs.

        add (jtPEER,          "peerCommand",
            0,        false,  true,  200,   2500);

        add (jtDISK,          "diskAccess",
            0,        false,  true,  500,   1000);

        add (jtTXN_PROC,      "processTransaction",
            0,        false,  true,  0,     0);

        add (jtOB_SETUP,      "orderBookSetup",
            0,        false,  true,  0,     0);

        add (jtPATH_FIND,     "pathFind",
            0,        false,  true,  0,     0);

        add (jtHO_READ,       "nodeRead",
            0,        false,  true,  0,     0);

        add (jtHO_WRITE,      "nodeWrite",
            0,        false,  true,  0,     0);

        add (jtGENERIC,       "generic",
            0,        false,  true,  0,     0);

        add (jtNS_SYNC_READ,  "SyncReadNode",
            0,        false,  true,  0,     0);
        add (jtNS_ASYNC_READ, "AsyncReadNode",
            0,        false,  true,  0,     0);
        add (jtNS_WRITE,      "WriteNode",
            0,        false,  true,  0,     0);

    }

    JobTypeInfo const& get (JobType jt) const
    {
        Map::const_iterator const iter (m_map.find (jt));
        assert (iter != m_map.end ());

        if (iter != m_map.end())
            return iter->second;

        return m_unknown;
    }

    JobTypeInfo const& getInvalid () const
    {
        return m_unknown;
    }

    const_iterator begin () const
    {
        return m_map.cbegin ();
    }

    const_iterator cbegin () const
    {
        return m_map.cbegin ();
    }

    const_iterator end () const
    {
        return m_map.cend ();
    }

    const_iterator cend () const
    {
        return m_map.cend ();
    }

private:
    void add(JobType jt, std::string name, int limit, 
        bool skip, bool special, std::uint64_t avgLatency, std::uint64_t peakLatency)
    {
        assert (m_map.find (jt) == m_map.end ());

        std::pair<Map::iterator,bool> result (m_map.emplace (
            std::piecewise_construct,
            std::forward_as_tuple (jt), 
            std::forward_as_tuple (jt, name, limit, skip, special,
                avgLatency, peakLatency)));

        assert (result.second == true);
    }

    JobTypeInfo m_unknown;
    Map m_map;
};

}

#endif
