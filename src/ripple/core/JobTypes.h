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
#include <string>
#include <type_traits>
#include <unordered_map>

namespace ripple {

class JobTypes
{
public:
    using Map = std::map<JobType, JobTypeInfo>;
    using const_iterator = Map::const_iterator;

private:
    JobTypes()
        : m_unknown(
              jtINVALID,
              "invalid",
              0,
              std::chrono::milliseconds{0},
              std::chrono::milliseconds{0})
    {
        using namespace std::chrono_literals;
        int maxLimit = std::numeric_limits<int>::max();

        auto add = [this](
                       JobType jt,
                       std::string name,
                       int limit,
                       std::chrono::milliseconds avgLatency,
                       std::chrono::milliseconds peakLatency) {
            assert(m_map.find(jt) == m_map.end());

            auto const [_, inserted] = m_map.emplace(
                std::piecewise_construct,
                std::forward_as_tuple(jt),
                std::forward_as_tuple(
                    jt, name, limit, avgLatency, peakLatency));

            assert(inserted == true);
            (void)_;
            (void)inserted;
        };

        // clang-format off
        add(jtPACK,          "makeFetchPack",                 1,     0ms,     0ms);
        add(jtPUBOLDLEDGER,  "publishAcqLedger",              2, 10000ms, 15000ms);
        add(jtVALIDATION_ut, "untrustedValidation",    maxLimit,  2000ms,  5000ms);
        add(jtTRANSACTION_l, "localTransaction",       maxLimit,   100ms,   500ms);
        add(jtREPLAY_REQ,    "ledgerReplayRequest",          10,   250ms,  1000ms);
        add(jtLEDGER_REQ,    "ledgerRequest",                 4,     0ms,     0ms);
        add(jtPROPOSAL_ut,   "untrustedProposal",      maxLimit,   500ms,  1250ms);
        add(jtREPLAY_TASK,   "ledgerReplayTask",       maxLimit,     0ms,     0ms);
        add(jtLEDGER_DATA,   "ledgerData",                    4,     0ms,     0ms);
        add(jtCLIENT,        "clientCommand",          maxLimit,  2000ms,  5000ms);
        add(jtRPC,           "RPC",                    maxLimit,     0ms,     0ms);
        add(jtUPDATE_PF,     "updatePaths",                   1,     0ms,     0ms);
        add(jtTRANSACTION,   "transaction",            maxLimit,   250ms,  1000ms);
        add(jtBATCH,         "batch",                  maxLimit,   250ms,  1000ms);
        add(jtADVANCE,       "advanceLedger",          maxLimit,     0ms,     0ms);
        add(jtPUBLEDGER,     "publishNewLedger",       maxLimit,  3000ms,  4500ms);
        add(jtTXN_DATA,      "fetchTxnData",                  5,     0ms,     0ms);
        add(jtWAL,           "writeAhead",             maxLimit,  1000ms,  2500ms);
        add(jtVALIDATION_t,  "trustedValidation",      maxLimit,   500ms,  1500ms);
        add(jtWRITE,         "writeObjects",           maxLimit,  1750ms,  2500ms);
        add(jtACCEPT,        "acceptLedger",           maxLimit,     0ms,     0ms);
        add(jtPROPOSAL_t,    "trustedProposal",        maxLimit,   100ms,   500ms);
        add(jtSWEEP,         "sweep",                         1,     0ms,     0ms);
        add(jtNETOP_CLUSTER, "clusterReport",                 1,  9999ms,  9999ms);
        add(jtNETOP_TIMER,   "heartbeat",                     1,   999ms,   999ms);
        add(jtADMIN,         "administration",         maxLimit,     0ms,     0ms);
        add(jtMISSING_TXN,   "handleHaveTransactions",     1200,     0ms,     0ms);
        add(jtREQUESTED_TXN, "doTransactions",             1200,     0ms,     0ms);

        add(jtPEER,          "peerCommand",                   0,   200ms,  2500ms);
        add(jtDISK,          "diskAccess",                    0,   500ms,  1000ms);
        add(jtTXN_PROC,      "processTransaction",            0,     0ms,     0ms);
        add(jtOB_SETUP,      "orderBookSetup",                0,     0ms,     0ms);
        add(jtPATH_FIND,     "pathFind",                      0,     0ms,     0ms);
        add(jtHO_READ,       "nodeRead",                      0,     0ms,     0ms);
        add(jtHO_WRITE,      "nodeWrite",                     0,     0ms,     0ms);
        add(jtGENERIC,       "generic",                       0,     0ms,     0ms);
        add(jtNS_SYNC_READ,  "SyncReadNode",                  0,     0ms,     0ms);
        add(jtNS_ASYNC_READ, "AsyncReadNode",                 0,     0ms,     0ms);
        add(jtNS_WRITE,      "WriteNode",                     0,     0ms,     0ms);
        // clang-format on
    }

public:
    static JobTypes const&
    instance()
    {
        static JobTypes const types;
        return types;
    }

    static std::string const&
    name(JobType jt)
    {
        return instance().get(jt).name();
    }

    JobTypeInfo const&
    get(JobType jt) const
    {
        Map::const_iterator const iter(m_map.find(jt));
        assert(iter != m_map.end());

        if (iter != m_map.end())
            return iter->second;

        return m_unknown;
    }

    JobTypeInfo const&
    getInvalid() const
    {
        return m_unknown;
    }

    Map::size_type
    size() const
    {
        return m_map.size();
    }

    const_iterator
    begin() const
    {
        return m_map.cbegin();
    }

    const_iterator
    cbegin() const
    {
        return m_map.cbegin();
    }

    const_iterator
    end() const
    {
        return m_map.cend();
    }

    const_iterator
    cend() const
    {
        return m_map.cend();
    }

    JobTypeInfo m_unknown;
    Map m_map;
};

}  // namespace ripple

#endif
