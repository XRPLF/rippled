#pragma once

#include <xrpl/core/Job.h>
#include <xrpl/core/JobTypeInfo.h>

#include <map>
#include <string>

namespace xrpl {

class JobTypes
{
public:
    using Map = std::map<JobType, JobTypeInfo>;
    using const_iterator = Map::const_iterator;

private:
    JobTypes()
        : unknown(
              JtInvalid,
              "invalid",
              0,
              std::chrono::milliseconds{0},
              std::chrono::milliseconds{0})
    {
        using namespace std::chrono_literals;
        int const maxLimit = std::numeric_limits<int>::max();

        auto add = [this](
                       JobType jt,
                       std::string name,
                       int limit,
                       std::chrono::milliseconds avgLatency,
                       std::chrono::milliseconds peakLatency) {
            XRPL_ASSERT(!map.contains(jt), "xrpl::JobTypes::JobTypes::add : unique job type input");

            [[maybe_unused]] auto const inserted =
                map.emplace(
                       std::piecewise_construct,
                       std::forward_as_tuple(jt),
                       std::forward_as_tuple(jt, name, limit, avgLatency, peakLatency))
                    .second;

            XRPL_ASSERT(inserted == true, "xrpl::JobTypes::JobTypes::add : input is inserted");
        };

        // clang-format off
        //                                                           avg     peak
        //  JobType               name                    limit    latency  latency
        add(JtPack,              "makeFetchPack",               1,     0ms,     0ms);
        add(JtPuboldledger,      "publishAcqLedger",            2, 10000ms, 15000ms);
        add(JtValidationUt,     "untrustedValidation",  maxLimit,  2000ms,  5000ms);
        add(JtManifest,          "manifest",             maxLimit,  2000ms,  5000ms);
        add(JtTransactionL,     "localTransaction",     maxLimit,   100ms,   500ms);
        add(JtReplayReq,        "ledgerReplayRequest",        10,   250ms,  1000ms);
        add(JtLedgerReq,        "ledgerRequest",               3,     0ms,     0ms);
        add(JtProposalUt,       "untrustedProposal",    maxLimit,   500ms,  1250ms);
        add(JtReplayTask,       "ledgerReplayTask",     maxLimit,     0ms,     0ms);
        add(JtLedgerData,       "ledgerData",                  3,     0ms,     0ms);
        add(JtClient,            "clientCommand",        maxLimit,  2000ms,  5000ms);
        add(JtClientSubscribe,  "clientSubscribe",      maxLimit,  2000ms,  5000ms);
        add(JtClientFeeChange, "clientFeeChange",      maxLimit,  2000ms,  5000ms);
        add(JtClientConsensus,  "clientConsensus",      maxLimit,  2000ms,  5000ms);
        add(JtClientAcctHist,  "clientAccountHistory", maxLimit,  2000ms,  5000ms);
        add(JtClientRpc,        "clientRPC",            maxLimit,  2000ms,  5000ms);
        add(JtClientWebsocket,  "clientWebsocket",      maxLimit,  2000ms,  5000ms);
        add(JtRpc,               "RPC",                  maxLimit,     0ms,     0ms);
        add(JtUpdatePf,         "updatePaths",                 1,     0ms,     0ms);
        add(JtTransaction,       "transaction",          maxLimit,   250ms,  1000ms);
        add(JtBatch,             "batch",                maxLimit,   250ms,  1000ms);
        add(JtAdvance,           "advanceLedger",        maxLimit,     0ms,     0ms);
        add(JtPubledger,         "publishNewLedger",     maxLimit,  3000ms,  4500ms);
        add(JtTxnData,          "fetchTxnData",                5,     0ms,     0ms);
        add(JtWal,               "writeAhead",           maxLimit,  1000ms,  2500ms);
        add(JtValidationT,      "trustedValidation",    maxLimit,   500ms,  1500ms);
        add(JtWrite,             "writeObjects",         maxLimit,  1750ms,  2500ms);
        add(JtAccept,            "acceptLedger",         maxLimit,     0ms,     0ms);
        add(JtProposalT,        "trustedProposal",      maxLimit,   100ms,   500ms);
        add(JtSweep,             "sweep",                       1,     0ms,     0ms);
        add(JtNetopCluster,     "clusterReport",               1,  9999ms,  9999ms);
        add(JtNetopTimer,       "heartbeat",                   1,   999ms,   999ms);
        add(JtAdmin,             "administration",       maxLimit,     0ms,     0ms);
        add(JtMissingTxn,       "handleHaveTransactions",   1200,     0ms,     0ms);
        add(JtRequestedTxn,     "doTransactions",           1200,     0ms,     0ms);

        add(JtPeer,              "peerCommand",                 0,   200ms,  2500ms);
        add(JtDisk,              "diskAccess",                  0,   500ms,  1000ms);
        add(JtTxnProc,          "processTransaction",          0,     0ms,     0ms);
        add(JtObSetup,          "orderBookSetup",              0,     0ms,     0ms);
        add(JtPathFind,         "pathFind",                    0,     0ms,     0ms);
        add(JtHoRead,           "nodeRead",                    0,     0ms,     0ms);
        add(JtHoWrite,          "nodeWrite",                   0,     0ms,     0ms);
        add(JtGeneric,           "generic",                     0,     0ms,     0ms);
        add(JtNsSyncRead,      "SyncReadNode",                0,     0ms,     0ms);
        add(JtNsAsyncRead,     "AsyncReadNode",               0,     0ms,     0ms);
        add(JtNsWrite,          "WriteNode",                   0,     0ms,     0ms);
        // clang-format on
    }

public:
    static JobTypes const&
    instance()
    {
        static JobTypes const kTypes;
        return kTypes;
    }

    static std::string const&
    name(JobType jt)
    {
        return instance().get(jt).name();
    }

    [[nodiscard]] JobTypeInfo const&
    get(JobType jt) const
    {
        Map::const_iterator const iter(map.find(jt));
        XRPL_ASSERT(iter != map.end(), "xrpl::JobTypes::get : valid input");

        if (iter != map.end())
            return iter->second;

        return unknown;
    }

    [[nodiscard]] JobTypeInfo const&
    getInvalid() const
    {
        return unknown;
    }

    [[nodiscard]] Map::size_type
    size() const
    {
        return map.size();
    }

    [[nodiscard]] const_iterator
    begin() const
    {
        return map.cbegin();
    }

    [[nodiscard]] const_iterator
    cbegin() const
    {
        return map.cbegin();
    }

    [[nodiscard]] const_iterator
    end() const
    {
        return map.cend();
    }

    [[nodiscard]] const_iterator
    cend() const
    {
        return map.cend();
    }

    JobTypeInfo unknown;
    Map map;
};

}  // namespace xrpl
