#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/core/ClosureCounter.h>
#include <xrpl/core/LoadMonitor.h>

#include <functional>

namespace xrpl {

// Note that this queue should only be used for CPU-bound jobs
// It is primarily intended for signature checking

// Protocol-wide
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum JobType {
    // Special type indicating an invalid job - will go away soon.
    JtInvalid = -1,

    // Job types - the position in this enum indicates the job priority with
    // earlier jobs having lower priority than later jobs. If you wish to
    // insert a job at a specific priority, simply add it at the right location.

    JtPack,             // Make a fetch pack for a peer
    JtPuboldledger,     // An old ledger has been accepted
    JtClient,           // A placeholder for the priority of all jtCLIENT jobs
    JtClientSubscribe,  // A websocket subscription by a client
    JtClientFeeChange,  // Subscription for fee change by a client
    JtClientConsensus,  // Subscription for consensus state change by a client
    JtClientAcctHist,   // Subscription for account history by a client
    JtClientRpc,        // Client RPC request
    JtClientWebsocket,  // Client websocket request
    JtRpc,              // A websocket command from the client
    JtSweep,            // Sweep for stale structures
    JtValidationUt,     // A validation from an untrusted source
    JtManifest,         // A validator's manifest
    JtUpdatePf,         // Update pathfinding requests
    JtTransactionL,     // A local transaction
    JtReplayReq,        // Peer request a ledger delta or a skip list
    JtLedgerReq,        // Peer request ledger/txnset data
    JtProposalUt,       // A proposal from an untrusted source
    JtReplayTask,       // A Ledger replay task/subtask
    JtTransaction,      // A transaction received from the network
    JtMissingTxn,       // Request missing transactions
    JtRequestedTxn,     // Reply with requested transactions
    JtBatch,            // Apply batched transactions
    JtLedgerData,       // Received data for a ledger we're acquiring
    JtAdvance,          // Advance validated/acquired ledgers
    JtPubledger,        // Publish a fully-accepted ledger
    JtTxnData,          // Fetch a proposed set
    JtWal,              // Write-ahead logging
    JtValidationT,      // A validation from a trusted source
    JtWrite,            // Write out hashed objects
    JtAccept,           // Accept a consensus ledger
    JtProposalT,        // A proposal from a trusted source
    JtNetopCluster,     // NetworkOPs cluster peer report
    JtNetopTimer,       // NetworkOPs net timer processing
    JtAdmin,            // An administrative operation

    // Special job types which are not dispatched by the job pool
    JtPeer,
    JtDisk,
    JtTxnProc,
    JtObSetup,
    JtPathFind,
    JtHoRead,
    JtHoWrite,
    JtGeneric,  // Used just to measure time

    // Node store monitoring
    JtNsSyncRead,
    JtNsAsyncRead,
    JtNsWrite,
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

    [[nodiscard]] JobType
    getType() const;

    /** Returns the time when the job was queued. */
    [[nodiscard]] clock_type::time_point const&
    queueTime() const;

    void
    doJob();

    // These comparison operators make the jobs sort in priority order
    // in the job set
    bool
    operator<(Job const& j) const;
    bool
    operator>(Job const& j) const;
    bool
    operator<=(Job const& j) const;
    bool
    operator>=(Job const& j) const;

private:
    JobType type_;
    std::uint64_t jobIndex_;
    std::function<void()> job_;
    std::shared_ptr<LoadEvent> loadEvent_;
    std::string name_;
    clock_type::time_point queueTime_;
};

using JobCounter = ClosureCounter<void>;

}  // namespace xrpl
