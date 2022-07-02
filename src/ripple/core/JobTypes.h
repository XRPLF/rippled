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
#include <array>
#include <chrono>
#include <string>

namespace ripple {

/** Defines the fixed attributes of a particular type of job.

    These attributes are defined at compile time and never change. They
    include things like the name of the job, the limit of concurrent jobs
    of this type, etc.
 */
struct JobTypeInfo
{
public:
    JobType const type;

    /// The job's display name (when we get C++20 this could be std::string)
    std::string_view const name;

    /** The limit on the number of running jobs for this job type.

        A limit of 0 marks this as a "special job" which is not
        dispatched via the job queue.
     */
    int const limit;

    /** Average and peak latencies for this job type, if relevant. */
    /** @{ */
    std::optional<std::chrono::milliseconds> const averageLatency;
    std::optional<std::chrono::milliseconds> const peakLatency;
    /** @} */

    // Not default constructible
    JobTypeInfo() = delete;

    constexpr JobTypeInfo(
        JobType type_,
        std::string_view const name_,
        int limit_,
        std::optional<std::chrono::milliseconds> averageLatency_ = {},
        std::optional<std::chrono::milliseconds> peakLatency_ = {})
        : type(type_)
        , name(name_)
        , limit(limit_)
        , averageLatency(averageLatency_)
        , peakLatency(peakLatency_)
    {
    }

    bool
    special() const
    {
        return limit == 0;
    }
};

// This namespace is a workaround to allow the use of chrono literals in the
// array initialization without injecting them into the broader namespace so
// that they are available everywhere.

namespace jobtypes_detail {

using namespace std::chrono_literals;

inline std::array<JobTypeInfo, 47> constexpr jobTypes{{
    // clang-format off
    //                                                                                  avg     peak
    //  JobType            name                                              limit  latency  latency
    { jtPACK,              "makeFetchPack",                                      1,                  },
    { jtPUBOLDLEDGER,      "publishAcqLedger",                                   2, 10000ms, 15000ms },
    { jtCLIENT,            "clientCommand",        std::numeric_limits<int>::max(),  2000ms,  5000ms },
    { jtCLIENT_SUBSCRIBE,  "clientSubscribe",      std::numeric_limits<int>::max(),  2000ms,  5000ms },
    { jtCLIENT_FEE_CHANGE, "clientFeeChange",      std::numeric_limits<int>::max(),  2000ms,  5000ms },
    { jtCLIENT_CONSENSUS,  "clientConsensus",      std::numeric_limits<int>::max(),  2000ms,  5000ms },
    { jtCLIENT_ACCT_HIST,  "clientAccountHistory", std::numeric_limits<int>::max(),  2000ms,  5000ms },
    { jtCLIENT_SHARD,      "clientShardArchive",   std::numeric_limits<int>::max(),  2000ms,  5000ms },
    { jtCLIENT_RPC,        "clientRPC",            std::numeric_limits<int>::max(),  2000ms,  5000ms },
    { jtCLIENT_WEBSOCKET,  "clientWebsocket",      std::numeric_limits<int>::max(),  2000ms,  5000ms },
    { jtRPC,               "RPC",                  std::numeric_limits<int>::max(),                  },
    { jtSWEEP,             "sweep",                                              1,                  },
    { jtVALIDATION_ut,     "untrustedValidation",  std::numeric_limits<int>::max(),  2000ms,  5000ms },
    { jtMANIFEST,          "manifest",             std::numeric_limits<int>::max(),  2000ms,  5000ms },
    { jtUPDATE_PF,         "updatePaths",                                        1,                  },
    { jtTRANSACTION_l,     "localTransaction",     std::numeric_limits<int>::max(),   100ms,   500ms },
    { jtREPLAY_REQ,        "ledgerReplayRequest",                               10,   250ms,  1000ms },
    { jtLEDGER_REQ,        "ledgerRequest",                                      3,                  },
    { jtPROPOSAL_ut,       "untrustedProposal",    std::numeric_limits<int>::max(),   500ms,  1250ms },
    { jtREPLAY_TASK,       "ledgerReplayTask",     std::numeric_limits<int>::max(),                  },
    { jtTRANSACTION,       "transaction",          std::numeric_limits<int>::max(),   250ms,  1000ms },
    { jtMISSING_TXN,       "handleHaveTransactions",                          1200,                  },
    { jtREQUESTED_TXN,     "doTransactions",                                  1200,                  },
    { jtBATCH,             "batch",                std::numeric_limits<int>::max(),   250ms,  1000ms },
    { jtLEDGER_DATA,       "ledgerData",                                         4,  2500ms,  5000ms },
    { jtADVANCE,           "advanceLedger",        std::numeric_limits<int>::max(),                  },
    { jtPUBLEDGER,         "publishNewLedger",     std::numeric_limits<int>::max(),  3000ms,  4500ms },
    { jtTXN_DATA,          "fetchTxnData",                                       5,                  },
    { jtWAL,               "writeAhead",           std::numeric_limits<int>::max(),  1000ms,  2500ms },
    { jtVALIDATION_t,      "trustedValidation",    std::numeric_limits<int>::max(),   500ms,  1500ms },
    { jtWRITE,             "writeObjects",         std::numeric_limits<int>::max(),  1750ms,  2500ms },
    { jtACCEPT,            "acceptLedger",         std::numeric_limits<int>::max(),                  },
    { jtPROPOSAL_t,        "trustedProposal",      std::numeric_limits<int>::max(),   100ms,   500ms },
    { jtNETOP_CLUSTER,     "clusterReport",                                      1,  9999ms,  9999ms },
    { jtNETOP_TIMER,       "heartbeat",                                          1,   999ms,   999ms },
    { jtADMIN,             "administration",       std::numeric_limits<int>::max(),                  },
    { jtPEER,              "peerCommand",                                        0,   200ms,  2500ms },
    { jtDISK,              "diskAccess",                                         0,   500ms,  1000ms },
    { jtTXN_PROC,          "processTransaction",                                 0                   },
    { jtOB_SETUP,          "orderBookSetup",                                     0                   },
    { jtPATH_FIND,         "pathFind",                                           0                   },
    { jtHO_READ,           "nodeRead",                                           0                   },
    { jtHO_WRITE,          "nodeWrite",                                          0                   },
    { jtGENERIC,           "generic",                                            0                   },
    { jtNS_SYNC_READ,      "SyncReadNode",                                       0                   },
    { jtNS_ASYNC_READ,     "AsyncReadNode",                                      0                   },
    { jtNS_WRITE,          "WriteNode",                                          0                   }
    // clang-format on
}};

// Ensure that the order of entries in the table corresponds to the
// order of entries in the enum:
static_assert(
    []() constexpr {
        std::underlying_type_t<JobType> idx = 0;

        for (auto const& jt : jobTypes)
        {
            if (static_cast<std::underlying_type_t<JobType>>(jt.type) != idx)
                return false;

            ++idx;
        }

        return true;
    }(),
    "The JobTypes array is either improperly sized or improperly ordered");

}  // namespace jobtypes_detail

using jobtypes_detail::jobTypes;

}  // namespace ripple

#endif
