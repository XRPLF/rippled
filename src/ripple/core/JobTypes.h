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

#ifndef RIPPLE_CORE_JOBTYPES_H_INCLUDED
#define RIPPLE_CORE_JOBTYPES_H_INCLUDED

#include <ripple/core/Job.h>
#include <ripple/core/JobTypeInfo.h>
#include <map>

namespace ripple
{

class JobTypes
{
public:
    using Map = std::map <JobType, JobTypeInfo>;
    using const_iterator = Map::const_iterator;

    JobTypes ()
        : m_unknown (jtINVALID, "invalid", 0, true, 0, 0)
    {
        int maxLimit = std::numeric_limits <int>::max ();

add(    jtPACK,          "makeFetchPack",           1,        false, 0,     0);
add(    jtPUBOLDLEDGER,  "publishAcqLedger",        2,        false, 10000, 15000);
add(    jtVALIDATION_ut, "untrustedValidation",     maxLimit, false, 2000,  5000);
add(    jtTRANSACTION_l, "localTransaction",        maxLimit, false, 100,   500);
add(    jtLEDGER_REQ,    "ledgerRequest",           2,        false, 0,     0);
add(    jtPROPOSAL_ut,   "untrustedProposal",       maxLimit, false, 500,   1250);
add(    jtLEDGER_DATA,   "ledgerData",              2,        false, 0,     0);
add(    jtCLIENT,        "clientCommand",           maxLimit, false, 2000,  5000);
add(    jtRPC,           "RPC",                     maxLimit, false, 0,     0);
add(    jtUPDATE_PF,     "updatePaths",             maxLimit, false, 0,     0);
add(    jtTRANSACTION,   "transaction",             maxLimit, false, 250,   1000);
add(    jtBATCH,         "batch",                   maxLimit, false, 250,   1000);
add(    jtADVANCE,       "advanceLedger",           maxLimit, false, 0,     0);
add(    jtPUBLEDGER,     "publishNewLedger",        maxLimit, false, 3000,  4500);
add(    jtTXN_DATA,      "fetchTxnData",            1,        false, 0,     0);
add(    jtWAL,           "writeAhead",              maxLimit, false, 1000,  2500);
add(    jtVALIDATION_t,  "trustedValidation",       maxLimit, false, 500,  1500);
add(    jtWRITE,         "writeObjects",            maxLimit, false, 1750,  2500);
add(    jtACCEPT,        "acceptLedger",            maxLimit, false, 0,     0);
add(    jtPROPOSAL_t,    "trustedProposal",         maxLimit, false, 100,   500);
add(    jtSWEEP,         "sweep",                   maxLimit, false, 0,     0);
add(    jtNETOP_CLUSTER, "clusterReport",           1,        false, 9999,  9999);
add(    jtNETOP_TIMER,   "heartbeat",               1,        false, 999,   999);
add(    jtADMIN,         "administration",          maxLimit, false, 0,     0);

add(    jtPEER,          "peerCommand",             0,        true,  200,   2500);
add(    jtDISK,          "diskAccess",              0,        true,  500,   1000);
add(    jtTXN_PROC,      "processTransaction",      0,        true,  0,     0);
add(    jtOB_SETUP,      "orderBookSetup",          0,        true,  0,     0);
add(    jtPATH_FIND,     "pathFind",                0,        true,  0,     0);
add(    jtHO_READ,       "nodeRead",                0,        true,  0,     0);
add(    jtHO_WRITE,      "nodeWrite",               0,        true,  0,     0);
add(    jtGENERIC,       "generic",                 0,        true,  0,     0);
add(    jtNS_SYNC_READ,  "SyncReadNode",            0,        true,  0,     0);
add(    jtNS_ASYNC_READ, "AsyncReadNode",           0,        true,  0,     0);
add(    jtNS_WRITE,      "WriteNode",               0,        true,  0,     0);

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
        bool special, std::uint64_t avgLatency, std::uint64_t peakLatency)
    {
        assert (m_map.find (jt) == m_map.end ());

        std::pair<Map::iterator,bool> result (m_map.emplace (
            std::piecewise_construct,
            std::forward_as_tuple (jt),
            std::forward_as_tuple (jt, name, limit, special,
                avgLatency, peakLatency)));

        assert (result.second == true);
        (void) result.second;
    }

    JobTypeInfo m_unknown;
    Map m_map;
};

}

#endif
