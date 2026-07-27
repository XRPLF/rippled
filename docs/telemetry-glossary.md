# Telemetry Glossary

Plain-language definitions of the XRP Ledger and `xrpld` terms used in the
Grafana dashboard panel descriptions. Each Grafana panel already carries a short
**Keywords** gloss inline; this page is the deeper reference for going further,
and most terms link out to the canonical [xrpl.org](https://xrpl.org/docs)
documentation.

> **Related docs**:
> [docs/telemetry-runbook.md](./telemetry-runbook.md) (operator runbook).

<!-- This file is generated from tasks/telemetry_terms.py. Edit the terms there. -->

## Contents

- [Ledger Lifecycle](#cat-ledger-lifecycle)
- [Consensus](#cat-consensus)
- [Transaction Pipeline](#cat-transaction-pipeline)
- [Fees & Queue](#cat-fees-queue)
- [Job Queue](#cat-job-queue)
- [Node State & Sync](#cat-node-state-sync)
- [Peer & Overlay Networking](#cat-peer-overlay-networking)
- [Storage Internals](#cat-storage-internals)
- [Validator Health](#cat-validator-health)
- [RPC & Pathfinding](#cat-rpc-pathfinding)

<a id="cat-ledger-lifecycle"></a>

## Ledger Lifecycle

<a id="ledger-build"></a>

### Ledger build

Building a ledger means applying the agreed transaction set, in canonical order, onto the previous ledger to produce the new closed ledger and its hash. Build time is a large component of the overall close time.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Ledger build on xrpl.org](https://xrpl.org/docs/concepts/ledgers/open-closed-validated-ledgers)

<a id="ledger-close"></a>

### Ledger close

The XRP Ledger never converts an open ledger into a closed one; instead the server discards the open ledger and builds a new closed ledger by applying the consensus-agreed transaction set (in canonical order) on top of the previous closed ledger. Consensus triggers the close; the close completes when the new ledger is built.

**Scope:** network event — a network-wide consensus process; this metric is one node's view of it.

**See also:** [Ledger close on xrpl.org](https://xrpl.org/docs/concepts/ledgers/open-closed-validated-ledgers)

<a id="ledger-close-interval"></a>

### Ledger close interval

The XRP Ledger closes a new ledger at a roughly steady cadence (about every 3-5 seconds on Mainnet). Close times are rounded to a shared resolution so validators can agree on them. A node that closes far slower or faster than the network cadence is not keeping up or is misbehaving.

**Scope:** network-wide — a protocol-shared value, the same across all nodes.

**See also:** [Ledger close interval on xrpl.org](https://xrpl.org/docs/concepts/ledgers/ledger-close-times)

<a id="ledger-index"></a>

### Ledger index

The ledger index (or ledger sequence) is the position of a ledger version in the chain, incremented by one for each new ledger. The current/open ledger index is one or two ahead of the latest validated sequence on a synced node.

**Scope:** network-wide — a protocol-shared value, the same across all nodes.

**See also:** [Ledger index on xrpl.org](https://xrpl.org/docs/references/protocol/data-types/basic-data-types#ledger-index)

<a id="ledger-store"></a>

### Ledger store

After a ledger is built and validated, the node persists it into its ledger history (the object store). The store rate should track the build rate on a healthy, in-sync node.

**Scope:** per node — measured on and specific to this individual server.

<a id="ledger-validation"></a>

### Ledger validation

Validation is the stage after transaction-set agreement: each server independently computes the ledger from the agreed set, then compares results. When enough trusted validators agree on the same ledger, it is declared validated (final and immutable). A validating node issues one validation per ledger it fully validates.

**Scope:** network event — a network-wide consensus process; this metric is one node's view of it.

**See also:** [Ledger validation on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/consensus-structure#validation)

<a id="open-ledger"></a>

### Open ledger

A server has exactly one open ledger: a temporary workspace where it provisionally applies transactions in the order received. Its results are tentative and can differ from the final validated result, because the closed ledger applies transactions in canonical order instead.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Open ledger on xrpl.org](https://xrpl.org/docs/concepts/ledgers/open-closed-validated-ledgers)

<a id="published-ledger"></a>

### Published ledger

After a ledger is validated the node publishes it to internal subscribers and clients. The published ledger normally tracks the validated ledger closely; a growing gap means the publish pipeline is backing up and subscribers may see stale data.

**Scope:** per node — measured on and specific to this individual server.

<a id="transaction-apply-phase"></a>

### Transaction apply phase

During a ledger close the server executes each transaction in the agreed set, in canonical order, updating ledger state. This apply phase (xrpld's doAccept path) is typically the largest single component of ledger-build time and scales with the number and cost of transactions in the ledger.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Transaction apply phase on xrpl.org](https://xrpl.org/docs/concepts/ledgers/open-closed-validated-ledgers)

<a id="validated-ledger"></a>

### Validated ledger

A validated ledger is one that a quorum of trusted validators has agreed on. It is immutable and forms part of the permanent ledger history. Each ledger index has exactly one validated ledger.

**Scope:** network-wide — a protocol-shared value, the same across all nodes.

**See also:** [Validated ledger on xrpl.org](https://xrpl.org/docs/concepts/ledgers/open-closed-validated-ledgers)

<a id="cat-consensus"></a>

## Consensus

<a id="clock-drift"></a>

### Clock drift

Because each validator stamps its own observed close time, differences between validator clocks (drift) spread the proposed close times, forcing coarser close-time resolution and more distinct positions.

**Scope:** network event — a network-wide consensus process; this metric is one node's view of it.

**See also:** [Clock drift on xrpl.org](https://xrpl.org/docs/concepts/ledgers/ledger-close-times)

<a id="close-time"></a>

### Close time

Each validator proposes the wall-clock time it saw the ledger close; validators then agree on a common close time, rounded to a shared resolution so their ledgers match. Disagreement on close time forces the rounding resolution coarser and can cause extra rounds.

**Scope:** network event — a network-wide consensus process; this metric is one node's view of it.

**See also:** [Close time on xrpl.org](https://xrpl.org/docs/concepts/ledgers/ledger-close-times)

<a id="close-time-resolution"></a>

### Close-time resolution

Close times are rounded to a shared resolution (a number of seconds) so validators can agree on a single value. When validators disagree on close time the resolution moves coarser (up toward 120s); when they agree tightly it moves finer. Repeated coarsening signals persistent close-time disagreement.

**Scope:** network event — a network-wide consensus process; this metric is one node's view of it.

**See also:** [Close-time resolution on xrpl.org](https://xrpl.org/docs/concepts/ledgers/ledger-close-times)

<a id="consensus"></a>

### Consensus

XRP Ledger consensus is an iterative agreement protocol: each server listens to its trusted validators and, when a supermajority agree on the same transaction set and close time, declares consensus and builds the ledger. If they disagree, validators revise proposals over successive rounds until they converge.

**Scope:** network event — a network-wide consensus process; this metric is one node's view of it.

**See also:** [Consensus on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/consensus-structure)

<a id="consensus-mode"></a>

### Consensus mode

Consensus mode reflects how a node is participating: Proposing (a validator advancing its own proposal), Observing (following without proposing), Wrong Ledger (working from a ledger the network disagrees with), or Switched Ledger (just changed to match the network). Sustained time in Wrong/Switched Ledger indicates the node is out of sync or flapping.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Consensus mode on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/consensus-structure)

<a id="consensus-outcome"></a>

### Consensus outcome

The result of a consensus round. Agreed is the healthy outcome. Moved On means the node proceeded without full agreement; Expired means the round timed out; No Consensus means agreement was not reached. A growing share of non-Agreed outcomes signals network stress or connectivity loss.

**Scope:** network event — a network-wide consensus process; this metric is one node's view of it.

**See also:** [Consensus outcome on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/consensus-principles-and-rules)

<a id="consensus-round"></a>

### Consensus round

A consensus round is one iteration in which validators relay and revise proposals. Multiple rounds (the establish count) may be needed within a single ledger before validators converge. Longer or more numerous rounds indicate disagreement, load, or poor connectivity.

**Scope:** network event — a network-wide consensus process; this metric is one node's view of it.

**See also:** [Consensus round on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/consensus-structure)

<a id="consensus-round-duration"></a>

### Consensus round duration

The wall-clock time one consensus round took, from its start to the ledger it accepted, as this node measured it. Recorded as a distribution rather than a single number, because the interesting question is not what one round took but how round times are spread and whether that spread is moving: a healthy network sits in a tight band a few seconds wide, and a band drifting upward delays every ledger behind it. Distinct from convergence time, which is how long validators took to agree on the transaction set — a round can converge quickly and still be slow overall if it spent its time waiting for the transaction set to arrive.

**Scope:** per node — measured on and specific to this individual server. The round is a network-wide process, but this is one node's own timing of it.

**See also:** [Consensus round](#consensus-round) · [Convergence time](#convergence-time) · [Tx-set acquire](#tx-set-acquire) · [Consensus round on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/consensus-structure)

<a id="consensus-stall"></a>

### Consensus stall

A stalled condition is flagged when consensus health checks detect that rounds are not progressing. A nonzero stall rate is an early warning that can precede ledger stalls or forks, surfacing before validated-ledger-age alarms fire.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Consensus stall on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/consensus-principles-and-rules)

<a id="convergence-time"></a>

### Convergence time

How long consensus took to converge on the agreed transaction set and close time, typically a few seconds. A rising convergence time indicates the network is taking longer to agree, often from load or connectivity problems.

**Scope:** network event — a network-wide consensus process; this metric is one node's view of it.

**See also:** [Convergence time on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/consensus-structure)

<a id="distinct-positions"></a>

### Distinct positions

A count of how many different close-time positions validators held in a round. Weight on a single distinct position means everyone agreed; weight on two or more means proposals split, indicating clock drift or latency spread across the validator set.

**Scope:** network event — a network-wide consensus process; this metric is one node's view of it.

**See also:** [Distinct positions on xrpl.org](https://xrpl.org/docs/concepts/ledgers/ledger-close-times)

<a id="establish-phase"></a>

### Establish phase

The establish phase is the part of consensus where validators exchange and revise proposals until they converge. The establish (iteration) count per ledger is normally low (a few iterations); a growing share of ledgers needing many iterations signals disagreement or network stress.

**Scope:** network event — a network-wide consensus process; this metric is one node's view of it.

**See also:** [Establish phase on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/consensus-structure)

<a id="fork"></a>

### Fork

A fork occurs when parts of the network validate different ledger chains. Sustained history mismatches or nodes stuck on the Wrong Ledger are fork indicators; the network is designed to avoid forks by requiring a trusted-validator quorum.

**Scope:** network-wide — a protocol-shared value, the same across all nodes.

**See also:** [Fork on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol)

<a id="ledger-history-mismatch"></a>

### Ledger history mismatch

A mismatch is recorded when the ledger this node built does not hash-match the ledger the network validated. Any sustained rate indicates consensus divergence or database corruption and warrants immediate investigation; the reason label distinguishes close-time, sync-drift, and transaction-processing causes.

**Scope:** per node — measured on and specific to this individual server.

<a id="position-update"></a>

### Position update

Each round a node tallies disputed transactions and updates its own proposed position to move toward its trusted peers. Sustained high position-update durations point to heavy dispute resolution or slow convergence.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Position update on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/consensus-structure)

<a id="proposal"></a>

### Proposal

During consensus each validator broadcasts a proposal: the set of candidate transactions it thinks should be in the next ledger. Validators revise proposals over rounds to match their trusted peers until a supermajority agree.

**Scope:** network event — a network-wide consensus process; this metric is one node's view of it.

**See also:** [Proposal on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/consensus-structure)

<a id="proposers"></a>

### Proposers

The number of distinct validators that proposed in the last consensus round. A falling proposer count (alongside rising convergence time) signals degrading consensus conditions, such as lost validator connectivity.

**Scope:** network event — a network-wide consensus process; this metric is one node's view of it.

**See also:** [Proposers on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/consensus-structure)

<a id="cat-transaction-pipeline"></a>

## Transaction Pipeline

<a id="apply-pipeline-stages"></a>

### Apply pipeline stages

A transaction is processed in stages: preflight validates it without ledger state (signature, format), preclaim checks it against current ledger state, and apply executes it and commits state changes. A failure spike concentrated in one stage pinpoints where transactions are being rejected.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Apply pipeline stages on xrpl.org](https://xrpl.org/docs/concepts/transactions)

<a id="direct-apply-vs-enqueue"></a>

### Direct apply vs enqueue

When a transaction arrives, the node either applies it directly to the open ledger (if it meets the open-ledger cost) or enqueues it for a future ledger. The bypass ratio is the share applied directly; it falls as congestion pushes more transactions into the queue.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Direct apply vs enqueue on xrpl.org](https://xrpl.org/docs/concepts/transactions/transaction-queue)

<a id="local-vs-relayed-transactions"></a>

### Local vs relayed transactions

Transactions either originate from a client submitting to this node (local) or are relayed from peers. Most traffic on a network node is peer-relayed; local dominates on a submission node. An unexpected surge in local submissions can indicate a client flooding the node.

**Scope:** per node — measured on and specific to this individual server.

<a id="queue-accept-drain"></a>

### Queue accept (drain)

When a ledger closes, the node drains eligible queued transactions into it. The accept/applied ratio is the share of drained transactions that were included versus removed on failure. A healthy drain applies most of them; a low ratio means accepts are mostly failing.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Queue accept (drain) on xrpl.org](https://xrpl.org/docs/concepts/transactions/transaction-queue)

<a id="transaction-result-codes"></a>

### Transaction result codes

Every transaction returns a result code grouped by prefix: tes (success), tec (failed but cost claimed, included in ledger), tef (cannot apply to this ledger or a later one), tem (malformed, cannot succeed in any ledger), ter (retry later), tel (local error). A steady background of tef/tec results is normal; a surge of one code for one type indicates a systemic issue or abusive submissions.

**Scope:** network-wide — a protocol-shared value, the same across all nodes.

**See also:** [Transaction result codes on xrpl.org](https://xrpl.org/docs/references/protocol/transactions/transaction-results)

<a id="transaction-suppression"></a>

### Transaction suppression

The same transaction reaches a node from many peers; the node suppresses (discards) copies it has already seen so they are not reprocessed. A large suppressed share is normal on a well-connected node; a collapse in suppression means duplicate filtering is failing.

**Scope:** per node — measured on and specific to this individual server.

<a id="transaction-type"></a>

### Transaction type

Each transaction has a type that determines its logic and cost, such as Payment, OfferCreate, TrustSet, or the AMM and NFToken families. Panels break down rate, latency, and failures by type; a single type spiking far above baseline can indicate a spam campaign of that type.

**Scope:** network-wide — a protocol-shared value, the same across all nodes.

**See also:** [Transaction type on xrpl.org](https://xrpl.org/docs/references/protocol/transactions/types)

<a id="transactor"></a>

### Transactor

A transactor is the code that applies a single transaction of a given type, running its type-specific checks and state changes. The transactor stage is the innermost apply step; its result is a transaction result code such as tesSUCCESS (the tes success class).

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Transactor on xrpl.org](https://xrpl.org/docs/references/protocol/transactions/transaction-results)

<a id="cat-fees-queue"></a>

## Fees & Queue

<a id="base-fee"></a>

### Base fee

The base fee is the transaction cost a reference (cheapest) transaction must destroy under minimum load, currently 10 drops on Mainnet. The actual required fee is the base fee scaled by the load factor and, when the open ledger is busy, by fee escalation.

**Scope:** network-wide — a protocol-shared value, the same across all nodes.

**See also:** [Base fee on xrpl.org](https://xrpl.org/docs/concepts/transactions/transaction-cost)

<a id="drops"></a>

### drops

A drop is the smallest denomination of XRP: 1 XRP = 1,000,000 drops. Fees, reserves, and costs are often reported in drops.

**Scope:** network-wide — a protocol-shared value, the same across all nodes.

**See also:** [drops on xrpl.org](https://xrpl.org/docs/references/protocol/data-types/basic-data-types#specifying-currency-amounts)

<a id="fee-escalation"></a>

### Fee escalation

Once the open ledger holds more than its soft target number of transactions, the cost to add further transactions rises exponentially (fee escalation). Transactions that cannot pay the escalated cost are queued instead. A large gap of the open-ledger level above the reference level means escalation is active.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Fee escalation on xrpl.org](https://xrpl.org/docs/concepts/transactions/transaction-cost#open-ledger-cost)

<a id="fee-levels"></a>

### Fee levels

Fee levels express the transaction cost relative to a transaction's own minimum, so they compare across transaction types. Key levels are reference (baseline, 256), minimum (to queue), median (of the last ledger), and open-ledger (to enter the current open ledger). The open-ledger level spiking far above reference is the hallmark of congestion.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Fee levels on xrpl.org](https://xrpl.org/docs/concepts/transactions/transaction-cost#fee-levels)

<a id="in-ledger-vs-target-count"></a>

### In-ledger vs target count

The node sets a soft target for how many transactions belong in a ledger, based on the previous ledger. While the in-ledger count stays at or below target, the open-ledger cost is minimal; exceeding it triggers exponential fee escalation.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [In-ledger vs target count on xrpl.org](https://xrpl.org/docs/concepts/transactions/transaction-cost#open-ledger-cost)

<a id="load-factor"></a>

### Load factor

The load factor is a multiplier applied to the base transaction cost; 1.0 means no load. It combines local server load, network load, and cluster load. A rising factor means the node is charging premium fees due to congestion or overload; the components identify where the pressure originates.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Load factor on xrpl.org](https://xrpl.org/docs/concepts/transactions/transaction-cost#local-load-cost)

<a id="queue-admission-rejection"></a>

### Queue admission rejection

When the queue is at capacity or a transaction is unlikely to be included, the node refuses it entry (a drop), applying backpressure. A burst of queue_full rejections, distinct from expiry, means the node is being flooded faster than it can drain.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Queue admission rejection on xrpl.org](https://xrpl.org/docs/concepts/transactions/transaction-queue)

<a id="queue-expiry-abandonment"></a>

### Queue expiry / abandonment

A queued transaction carrying a LastLedgerSequence is dropped once that deadline passes without inclusion. A sustained expiry rate is a demand-frustration signal: submitters under-bid the escalating fee and their transactions timed out, often coinciding with spam.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Queue expiry / abandonment on xrpl.org](https://xrpl.org/docs/concepts/transactions/reliable-transaction-submission)

<a id="reserve-base-owner"></a>

### Reserve (base & owner)

Reserves protect the ledger from spam by requiring accounts to hold XRP. The base reserve is the minimum per account; the owner (incremental) reserve adds a further requirement per object the account owns (offers, trust lines, escrows, etc.). Both are set by validator fee voting and reported in drops.

**Scope:** network-wide — a protocol-shared value, the same across all nodes.

**See also:** [Reserve (base & owner) on xrpl.org](https://xrpl.org/docs/concepts/accounts/reserves)

<a id="transaction-cost"></a>

### Transaction cost

Every transaction must destroy a small amount of XRP (the transaction cost) to be relayed and included, which deters spam. The cost is the base fee scaled by the current load factor, and rises further under fee escalation when the open ledger is congested.

**Scope:** network-wide — a protocol-shared value, the same across all nodes.

**See also:** [Transaction cost on xrpl.org](https://xrpl.org/docs/concepts/transactions/transaction-cost)

<a id="transaction-queue-txq"></a>

### Transaction queue (TxQ)

The transaction queue (TxQ) holds transactions that pay enough for local relay but not the current open-ledger cost, so the node can include them in a future ledger instead of discarding them. Depth pinned at capacity for sustained periods signals demand exceeding throughput or a fee-spam burst.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Transaction queue (TxQ) on xrpl.org](https://xrpl.org/docs/concepts/transactions/transaction-queue)

<a id="cat-job-queue"></a>

## Job Queue

<a id="concurrency-limit"></a>

### Concurrency limit

Each job type declares how many of its jobs may run at the same time. Sync-critical types are deliberately tight so one kind of work cannot monopolize the worker pool. A type sitting at its limit cannot start more work even when worker threads are idle, which makes the limit a distinct kind of bottleneck from CPU or disk.

**Scope:** per node — measured on and specific to this individual server.

<a id="handler-label"></a>

### Handler label

Several call sites can enqueue work under the same job type, so job type alone cannot say which one caused a latency spike. The handler label carries the name of the call site that enqueued the job. Names are kept only when they are letters-only; anything else — including names that embed a ledger sequence number — folds into a shared `other` bucket, which keeps the number of series bounded. A reading under `other` therefore mixes several callers and never identifies one.

**Scope:** per node — measured on and specific to this individual server.

<a id="job-queue-job-type"></a>

### Job queue / job type

The job queue is the worker-thread pool that runs xrpld's background work. Every unit of work is enqueued under a named job type — serving a peer's ledger request, absorbing inbound ledger data, updating payment paths, and so on — and each type is accounted separately: waiting, running, and deferred. Types that carry sync-critical traffic are the ones worth watching, because a backlog there translates directly into the node falling behind.

**Scope:** per node — measured on and specific to this individual server.

<a id="cat-node-state-sync"></a>

## Node State & Sync

<a id="back-fill-catch-up"></a>

### Back-fill / catch-up

When a node is missing ledgers (at startup, after an outage, or to extend history) it back-fills by fetching them from peers. Elevated back-fill activity is normal while catching up and should fall to near zero once history is complete and the node is synced.

**Scope:** per node — measured on and specific to this individual server.

<a id="byzantine-ledger-jump"></a>

### Byzantine ledger jump

Being told that the network's last closed ledger is not the one this node built on, and discarding its own chain tip to follow the network instead. It is an abnormal event by construction: the node had already closed a ledger, and it is now throwing that work away because the peers it listens to agree on a different one. A single jump while a fresh node is still settling onto the network's chain can be benign. Repeated jumps are wrong-chain thrash — the node keeps switching between chains and never settles — and the cause is upstream of the sync pipeline, in which peers it is listening to or which network it thinks it is on, so nothing in ledger acquisition can fix it.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Fork](#fork) · [Ledger history mismatch](#ledger-history-mismatch) · [Insane / diverged peers](#insane-diverged-peers)

<a id="clock-close-offset"></a>

### Clock close offset

The difference between the network's agreed ledger close time and this node's own clock, negative when the local clock runs ahead and positive when it lags. A magnitude above a second that does not decay delays consensus participation and is a local time-sync fault rather than a network one; the server-info API only reports the offset once the magnitude reaches 60 seconds, so the metric sees skew far earlier.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Clock drift](#clock-drift) · [Ledger close times on xrpl.org](https://xrpl.org/docs/concepts/ledgers/ledger-close-times)

<a id="complete-ledger-ranges"></a>

### Complete ledger ranges

A node stores ledger history as one or more contiguous ranges. One continuous range means an unbroken history; many fragmented ranges indicate gaps from missed or failed fetches that the node is still back-filling.

**Scope:** per node — measured on and specific to this individual server.

<a id="dns-resolve"></a>

### DNS resolve

Turning each configured peer hostname into IP addresses, which happens before any connection is attempted. An empty outcome means the name resolved to nothing, so that peer is never dialled at all; slow resolution delays every dial behind it even when it eventually succeeds.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Outbound dial latency](#outbound-dial-latency) · [Peer protocol on xrpl.org](https://xrpl.org/docs/concepts/networks-and-servers/peer-protocol)

<a id="deferred-job"></a>

### Deferred job

A job the queue accepted but withheld from a worker thread because its job type is already running at that type's concurrency limit. This is a third state alongside waiting and running, and it is counted in neither: the work exists and is being actively denied a thread, which is starvation rather than idleness or overload. The distinction matters because the sync-critical types run at very small limits — ledger requests and inbound ledger data are each capped at three concurrent jobs — so during a fresh sync those types routinely have work withheld while the queue looks shallow and the queue-wait quantiles look unremarkable. A sustained non-zero deferred count names the job type whose limit is the bottleneck: each completing job releases one withheld job, so a count that stays high means arrivals are outpacing completions.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Job queue occupancy](#job-queue-occupancy) · [Worker-pool saturation](#worker-pool-saturation)

<a id="acquire-phase"></a>

### Acquire phase

One of the three sequential fetches a ledger acquisition is made of: the ledger header, then the account-state tree, then the transaction tree. The header comes first and gates the other two, because it is what names their root hashes — until it arrives the node does not yet know what to ask for. The distinction matters because the three fail for different reasons and at wildly different scales: on a fresh node the account-state tree is nearly all of the work, so an acquisition measured as a whole reports essentially that tree alone, and a node stuck waiting for the header or for the far smaller transaction tree looks identical to one making normal progress. Measured per phase, the phase that is stuck names itself. A phase can also end because its retry budget expired while the acquisition as a whole is still alive and retrying, which is why running out of time is recorded separately from failing.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Ledger acquire (inbound fetch)](#ledger-acquire-inbound-fetch) · [Missing SHAMap node](#missing-shamap-node) · [Acquire stall](#acquire-stall)

<a id="acquire-source"></a>

### Acquire source

Whether a ledger acquire was satisfied entirely from the local node store or required fetching the data from peers. During a genuine fresh sync almost every acquire is network-sourced, because nothing is local yet. The signal becomes diagnostic on a node that should already hold the range: acquires that still go to the network mean the local store is not retaining data, so the slowness is disk-bound rather than peer-bound. This is the pairing that explains why a node with a large existing database can start slower than a fresh one.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [SHAMap cache hit rate](#shamap-cache-hit-rate) · [Ledger acquire (inbound fetch)](#ledger-acquire-inbound-fetch)

<a id="acquire-stall"></a>

### Acquire stall

A ledger-acquire timeout in which not a single new tree node arrived since the previous timeout, so the acquire made no progress at all. Distinct from a slow acquire, which still receives data between timeouts. A sustained stall rate alongside a missing-node count that never falls is the definitive "this sync will never complete" signature: the node keeps asking and no peer answers.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Missing SHAMap node](#missing-shamap-node) · [Ledger acquire (inbound fetch)](#ledger-acquire-inbound-fetch)

<a id="add-node-outcome"></a>

### Add-node outcome

The result of applying one SHAMap node received from a peer during a ledger acquire: good (new and valid), duplicate (already held), or invalid (failed validation). The split matters because traffic-level metrics count all three as healthy throughput. Only good represents progress; a duplicate share that swamps it means peers keep re-sending data the node already has, and a rising invalid share points at one misbehaving peer rather than a local fault.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Received-data stash](#received-data-stash) · [Missing SHAMap node](#missing-shamap-node)

<a id="fresh-node-sync-diagnostics"></a>

### Fresh-node sync diagnostics

The set of signals that explain why a freshly-started node is slow to reach, or never reaches, a validated ledger. They split into pre-quorum bootstrap (DNS, peer dial, protocol negotiation, UNL fetch and quorum, clock skew) and the post-peering acquire pipeline (sync state, ledger and tx-set acquire, job queue, quorum and publish lag, back-fill, persistence). Rendered by the Ledger Sync Health dashboard; individual terms are defined below as each signal lands.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Diagnosing slow/stuck fresh sync](./telemetry-runbook.md#diagnosing-slowstuck-fresh-sync) (operator flow) · [Server states on xrpl.org](https://xrpl.org/docs/references/http-websocket-apis/api-conventions/xrpld-server-states)

<a id="handshake-negotiation-failure"></a>

### Handshake negotiation failure

A peer connection rejected after TLS succeeds, while the two sides check network identifier, clock, keys and reported addresses. The rejection reason names the failing check: a wrong network identifier means the node can never reach a quorum with those peers, a clock reason points at local time sync, and key or signature reasons point at the peer.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Outbound dial latency](#outbound-dial-latency) · [Clock close offset](#clock-close-offset) · [Peer protocol on xrpl.org](https://xrpl.org/docs/concepts/networks-and-servers/peer-protocol)

<a id="historical-fetch-rate"></a>

### Historical fetch rate

The rate at which the node fetches older ledgers to extend or repair its stored history. Elevated while back-filling; near zero once history is complete.

**Scope:** per node — measured on and specific to this individual server.

<a id="job-queue-occupancy"></a>

### Job queue occupancy

How many jobs of a given type are queued or executing at the instant the queue is sampled, as opposed to how many passed through it over a period. Occupancy answers "what is sitting there now"; the job counters and queue-wait quantiles answer "what already moved and how long it had waited". The two can disagree in the way that matters most: a type whose jobs are all still queued produces no completed-job samples at all, so a latency quantile can look healthy precisely because nothing is finishing.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Deferred job](#deferred-job) · [Worker-pool saturation](#worker-pool-saturation)

<a id="ledger-acquire-inbound-fetch"></a>

### Ledger acquire (inbound fetch)

Acquiring a ledger means requesting it and its contents from peers when the node lacks it. Acquire outcomes split into complete and failed; a rising failed rate means the node cannot fetch needed ledgers from its peers.

**Scope:** per node — measured on and specific to this individual server.

<a id="ledger-replay"></a>

### Ledger replay

An optional faster way to rebuild a run of historical ledgers: instead of downloading each ledger whole, the node fetches one starting ledger plus the list of ledger hashes that links the range, then fetches only what changed in each subsequent ledger and applies those changes on top of its predecessor. It is only available when enough connected peers support the protocol feature that serves those pieces, so whether it is used at all depends on the peer set rather than on local configuration alone.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Replay fallback](#replay-fallback) · [Ledger acquire (inbound fetch)](#ledger-acquire-inbound-fetch)

<a id="ledger-serve"></a>

### Ledger serve

This node answering a peer's request for ledger data, as opposed to requesting data for itself. It is the supply side of the same exchange every other sync term describes from the receiving side, and it explains a peer's sync rather than this node's own: a server that answers nothing is why some other operator sees no peer able to serve the range they need. Worth reading in two ways. Against the receiving side, healthy serving alongside starved receiving points at peer selection rather than at this node's capacity. On its own, the kind of object asked for matters, because a request for the account-state tree is the expensive one a syncing peer actually depends on, while header replies are cheap. A reply can also be cut short by a size limit rather than refused, which is not a failure but does mean the requesting peer has to come back for the remainder — several round trips for one tree.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Ledger acquire (inbound fetch)](#ledger-acquire-inbound-fetch) · [Acquire phase](#acquire-phase) · [Peer ledger supply](#peer-ledger-supply)

<a id="ledgers-behind-network"></a>

### Ledgers behind network

How many ledgers this node's validated sequence trails the network's. The network figure is the highest ledger sequence any connected peer reports holding, so the gap is what the node still has to close to reach the tip. Trending down to zero is healthy convergence; flat or rising means the node acquires slower than the network advances and will not converge on its own. Because the value is floored at zero and the target comes from peer reports, a node with no peers — or whose peers have not reported a range yet — also reads zero, so a zero is only "at the tip" once there are peers.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Time to first FULL](#time-to-first-full) · [Ledger acquire (inbound fetch)](#ledger-acquire-inbound-fetch) · [Validated ledger on xrpl.org](https://xrpl.org/docs/concepts/ledgers/open-closed-validated-ledgers)

<a id="missing-shamap-node"></a>

### Missing SHAMap node

A node of a ledger's account-state or transaction tree that this server needs in order to complete the ledger but does not yet hold. The count of outstanding missing nodes is the clearest available answer to "is this acquire progressing?": a count falling toward zero is progress, while a count that stays flat and non-zero means no peer is serving that tree and the acquire will never finish. Reported per tree, as the maximum across in-flight acquires, and capped per sweep — so a value sitting at the cap means the real backlog is at least that large, and only the trend distinguishes a large-but-progressing tree from a stuck one.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Acquire stall](#acquire-stall) · [Received-data stash](#received-data-stash) · [Ledger acquire (inbound fetch)](#ledger-acquire-inbound-fetch)

<a id="network-ledger-gate"></a>

### Network ledger gate

The startup guard that holds a node back until it has seen a complete ledger from the network. While the gate is closed the node refuses submitted transactions and cannot reach the full state, no matter how healthy the rest of the sync pipeline looks. It normally opens within the first minutes of startup; a gate that stays closed means the node never obtained a full network ledger, which is a peering or quorum fault upstream rather than a sync-pipeline one.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Operating mode / server state](#operating-mode-server-state) · [UNL quorum headroom](#unl-quorum-headroom) · [Operating mode / server state on xrpl.org](https://xrpl.org/docs/references/http-websocket-apis/api-conventions/xrpld-server-states)

<a id="node-store-read-latency"></a>

### Node-store read latency

How long the node store takes to return one stored object. Every ledger traversal that is not already answered from an in-memory cache pays this cost, so it is the floor under ledger acquisition and under most queries. It is reported as an average over an interval rather than as a distribution, which means a slow minority of reads shows up as a raised average rather than as a separate tail figure.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Node-store write latency](#node-store-write-latency) · [SHAMap cache hit rate](#shamap-cache-hit-rate)

<a id="node-store-operation-rate"></a>

### Node-store operation rate

How many objects per second the node store is storing and retrieving. It is the companion an average latency needs in order to be read correctly: latency measured over an interval with almost no operations in it is a stale number rather than a good one, and a node writing nothing at all while still behind the network is stalled somewhere upstream of storage rather than slowed by it.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Node-store write latency](#node-store-write-latency)

<a id="node-store-write-latency"></a>

### Node-store write latency

How long the node store takes to persist one object. This is the cost that governs how fast a node can absorb ledger history, because filling in history is dominated by writing rather than by reading. It is the measurement that distinguishes the two ways a sync can be slow: starved of data from peers, or unable to write down the data it already has. A node with a large existing database can be slower to start and catch up than an empty one for exactly this reason, and no read-side measurement reveals it. Like the read figure it is an interval average, not a distribution.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Node-store read latency](#node-store-read-latency) · [Node-store operation rate](#node-store-operation-rate)

<a id="heap-trim"></a>

### Heap trim

Asking the memory allocator to return free pages from its own pools back to the operating system. The node does this at the end of every periodic cache sweep, because a sweep is exactly when a large amount of memory has just been released. The cost is not fixed: the allocator has to walk its pools to find what is returnable, so the work grows with how much memory the process is holding — which is why a node with a large existing database pays more for it, every sweep, than an empty one does. The pages handed back are not gone for good; the next access to that memory has to take them again, which is the reason a trim is a trade rather than a pure saving.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Minor page fault](#minor-page-fault) · [Reclaimed resident memory](#reclaimed-resident-memory) · [Sweep interval](#sweep-interval)

<a id="minor-page-fault"></a>

### Minor page fault

A memory access the operating system satisfies without touching a disk, by attaching a page it already had available. Cheap next to a disk read but not free, and taken in volume it becomes a real cost. Faults counted during a heap trim show the trim doing its own work of releasing memory. They deliberately say nothing about the faults paid afterwards, when caches refill and touch the memory that was given back — that later cost is the reason a trim can slow other work down, and counting the faults inside the trim does not capture it. Reading the figure as the total price of trimming overstates what was measured.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Heap trim](#heap-trim) · [Reclaimed resident memory](#reclaimed-resident-memory)

<a id="reclaimed-resident-memory"></a>

### Reclaimed resident memory

How much memory a heap trim actually handed back to the operating system, as opposed to how long it spent looking. This is what makes the trim's cost judgeable: time spent with memory returned is a trade, and time spent with nothing returned is pure loss. Only memory the allocator holds in its own pools can be returned at all, so a trim can legitimately reclaim nothing — and a reading of zero is a real answer rather than a missing one. Memory can also grow across a trim, when other threads allocate faster than it releases; that is reported as no reclaim rather than as a negative amount, since a running total cannot go backwards.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Heap trim](#heap-trim) · [Sweep interval](#sweep-interval)

<a id="sweep-interval"></a>

### Sweep interval

How often the node runs its periodic pass over the in-memory caches, expiring what is stale. The period is chosen from the configured node size, so a larger node sweeps less often. It sets the cadence of everything the sweep does, including the heap trim at the end of it, and it is therefore the number against which any per-sweep cost has to be judged: the same expense is negligible at one interval and significant at another. The sweep runs as a queued job, so its cost is paid on a worker thread and competes with other work rather than happening in the background.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Heap trim](#heap-trim) · [Job queue occupancy](#job-queue-occupancy)

<a id="rotation-window"></a>

### Rotation window

The interval during which the node is swapping the pair of storage backends that make online deletion of old history possible. New data goes to one backend while the older one is kept for reading; on a swap the older one is discarded and a fresh one takes over. The window matters because it is the only time certain extra writes happen, so a cost seen inside it and a cost seen outside it have different explanations. A node that is not configured for online deletion has no window at all, which is a different situation from a window that costs nothing.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Copy-forward write](#copy-forward-write) · [Node re-store](#node-re-store) · [Node-store write latency](#node-store-write-latency)

<a id="copy-forward-write"></a>

### Copy-forward write

Rewriting a stored object out of the backend that is about to be discarded and into the one replacing it. An ordinary read would not write anything; this one must, because the copy it just read is about to be deleted and would otherwise survive only in memory. The volume scales with how much of the outgoing backend gets read during the swap, so it is a cost only a node that already holds history can incur — the reason this competes with catching up on a populated database and never appears on a fresh one.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Rotation window](#rotation-window) · [Node re-store](#node-re-store)

<a id="node-re-store"></a>

### Node re-store

Writing a tree node back to storage from memory because it could not be found in either storage backend. It signals more than cost. The node is still reachable from the current validated state, yet its only stored copy was in a backend an earlier swap discarded, and it was never rewritten because nothing had modified it. Rescuing it is an extra write, and skipping the rescue would leave the node unresolvable later. A sustained rate therefore reports two things at once: added write pressure now, and history quietly dropped by an earlier swap.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Rotation window](#rotation-window) · [Copy-forward write](#copy-forward-write) · [Missing SHAMap node](#missing-shamap-node)

<a id="operating-mode-server-state"></a>

### Operating mode / server state

The server state describes how fully the node is participating, in ascending order: disconnected, connected, syncing, tracking, full (caught up), and for validators validating and proposing. A healthy non-validator sits in Full; frequent transitions out of Full indicate instability. Transitions are recorded as a from-to edge rather than a bare count, which is what distinguishes a clean one-way climb to Full from flapping in and out of it.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Time to first FULL](#time-to-first-full) · [Network ledger gate](#network-ledger-gate) · [Mode flapping](#mode-flapping) · [Operating mode / server state on xrpl.org](https://xrpl.org/docs/references/http-websocket-apis/api-conventions/xrpld-server-states)

<a id="mode-flapping"></a>

### Mode flapping

Mode flapping is a node repeatedly reaching the full server state and losing it again, rather than climbing to it once and staying. It is visible only because state transitions are recorded as a from-to edge: a clean fresh sync traverses each climb edge roughly once, whereas flapping shows repeated counts on a reverse edge paired with its forward partner. A bare transition count cannot distinguish the two, which is why the edge labels exist. Flapping alongside healthy ledger acquisition points away from the acquire pipeline and at whatever drops a node out of full once it has arrived — a stalling main loop, a clock disagreeing with the network, or a trusted list that keeps failing quorum.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Operating mode / server state](#operating-mode-server-state) · [Server stall](#server-stall) · [Clock close offset](#clock-close-offset) · [Validation quorum](#validation-quorum)

<a id="sentinel-reading"></a>

### Sentinel reading

A sentinel reading is a deliberately out-of-range value a gauge reports to mean "this condition does not apply", chosen so the healthy or unknown state is a distinct value rather than a missing series. The distinction matters because these are observable gauges: they report on every collection tick whatever the value, so absence is a regression while an unusual number may be the intended answer. Three appear in the sync diagnostics: an amendment-block countdown of -1 means nothing is pending, and is not a negative duration; a quorum target at the signed 64-bit maximum means quorum has been switched off entirely because too many list publishers are unavailable, reported as that maximum rather than allowed to wrap negative so it cannot be misread as a target already exceeded; and a peer supply window of zero means no peer has advertised a range yet, meaning unknown rather than the start of history. A one-shot duration reading of zero is the related case — it means the milestone was never reached, not that it took no time.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Amendment block countdown](#amendment-block-countdown) · [Validation quorum](#validation-quorum) · [Peer ledger supply](#peer-ledger-supply) · [Time to first FULL](#time-to-first-full) · [Time to first validated ledger](#time-to-first-validated-ledger)

<a id="outbound-dial-latency"></a>

### Outbound dial latency

The elapsed time of one outbound peer connection attempt, from starting the TCP connect through TLS to the protocol upgrade, measured to whichever outcome ends it. Every attempt ends in exactly one outcome, so the outcome names the stage that broke; because the timing covers failures too, a value pinned near the dial timeout means peers accept the connection but never finish the handshake.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [DNS resolve](#dns-resolve) · [Handshake negotiation failure](#handshake-negotiation-failure) · [Peer protocol on xrpl.org](https://xrpl.org/docs/concepts/networks-and-servers/peer-protocol)

<a id="peer-ledger-supply"></a>

### Peer ledger supply

The idea that a connected peer set collectively offers a window of ledger sequences, rather than being simply present or absent. Each peer advertises the oldest and newest ledger it holds, so the set as a whole can serve some range and nothing outside it. This turns "how many peers do I have" into the question that actually matters during a sync: does any connected peer hold the next ledger this node needs. Being unable to advance because nobody holds that sequence is a fundamentally different fault from being slow — it is a supply gap fixed only by changing the peer set, whereas slowness with the data available is a throughput problem fixed locally, and the two are indistinguishable from inside the acquire itself. The shape of a gap matters too: needing a sequence below the window means asking for history nobody kept, while needing one above it means asking for a tip nobody has reached. Peers that have not advertised a range yet are excluded from the counts entirely, so a zero window means unknown rather than empty, and the count of peers that have reported anything is what makes the rest readable.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Ledgers behind network](#ledgers-behind-network) · [Slot census](#slot-census) · [Acquire stall](#acquire-stall) · [Complete ledger ranges](#complete-ledger-ranges)

<a id="received-data-stash"></a>

### Received-data stash

Peer packets held for later processing because a ledger acquire cannot apply them as fast as they arrive. A shallow stash means node data is applied as it lands. A growing stash means the bottleneck is local processing — job-queue depth or disk latency — rather than peer supply, which is the opposite conclusion from an acquire that receives nothing at all. Read alongside the in-flight acquire count, since an empty stash on an idle node says nothing about acquire health.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Add-node outcome](#add-node-outcome) · [Acquire stall](#acquire-stall)

<a id="replay-fallback"></a>

### Replay fallback

A replay sub-task giving up on the delta shortcut and acquiring the entire ledger instead, which happens when too few connected peers support the feature that serves the pieces replay needs. Nothing fails when this occurs and no error is raised — the node still completes its back-fill, just on the slower path — which is why it is easy to miss: the optimisation is simply absent. It is counted separately for each of the two sub-tasks, because the one that fetches the list of historical ledger hashes and the one that fetches a single ledger's changes can fail independently of each other.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Ledger replay](#ledger-replay)

<a id="shamap-cache-hit-rate"></a>

### SHAMap cache hit rate

The share of SHAMap tree-node lookups answered from the in-memory tree-node cache rather than the node store. This is the layer above the node store's own hit ratio: a miss here is what causes a node-store read there. A low rate is expected during a fresh sync while the cache fills. A persistently low rate on a node that should be warm means the working set does not fit the cache, or continuous re-acquisition is churning it, so every tree walk pays disk latency.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Acquire source](#acquire-source) · [Missing SHAMap node](#missing-shamap-node)

<a id="server-stall"></a>

### Server stall

The server's main loop failing to check in with the load monitor, measured as seconds of unresponsiveness. A stall means main-loop overload, not a shortage of sync data, so the cause is downstream work such as job-queue backlog or slow disk rather than peer supply. Two readings mean different things: a large duration with a flat episode count is one long unresolved stall, while a small duration with a rising episode count is repeated short stalls the server keeps recovering from. Episodes are counted once per stall, not once per stalled second, which is what keeps those two cases distinguishable. A stall that persists long enough is treated as unrecoverable and deliberately ends the process.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Ledgers behind network](#ledgers-behind-network) · [Consensus stall](#consensus-stall)

<a id="time-to-first-full"></a>

### Time to first FULL

The elapsed time from process start until the node first reached the full server state. It is a one-shot measurement: it is set on the first transition to full and never changes afterwards, so it has no trend to read. That leaves exactly two meaningful readings — a duration, meaning the node synced and this is how long it took, or zero, meaning it has never reached full at all. The zero is the diagnostic signal rather than absent data, and it is the starting point for working through the rest of the sync pipeline.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Operating mode / server state](#operating-mode-server-state) · [Network ledger gate](#network-ledger-gate) · [Ledgers behind network](#ledgers-behind-network)

<a id="tx-set-acquire"></a>

### Tx-set acquire

One attempt to fetch the set of transactions a consensus proposal referred to but this node did not already hold. It is the consensus-path sibling of a ledger acquire — the same retry-and-timeout machinery, fetching a transaction tree rather than a whole ledger — and it is on the critical path of a round: until the set arrives, the node cannot evaluate the position that referenced it. This makes it a distinct kind of sync problem from history back-fill, and one that was previously invisible: a round waiting on a set that never arrives is indistinguishable from an idle node unless the attempt itself is measured. Two readings are diagnostic. Attempts that never complete mean rounds are blocked on data rather than on disagreement. Attempts that do complete but take about as long as a round means sets arrive so late that they delay the round they belong to — which the completion rate alone cannot reveal, because those attempts succeed. Zero attempts is normal and healthy: it means the node already held every set proposed to it.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Ledger acquire (inbound fetch)](#ledger-acquire-inbound-fetch) · [Consensus round](#consensus-round) · [Acquire phase](#acquire-phase)

<a id="per-ledger-trace-join"></a>

### Per-ledger trace join

The scheme that makes every span touching one ledger appear in a single trace, so one slow ledger can be read as one unit. The spans are produced on threads that share nothing — the network fetch on a job-queue worker, the acceptance decision on whichever thread received the validation that triggered it, the persist on a third — so none of them can inherit a parent from the others. Instead each derives its trace identifier from the same value they all already hold: the ledger's own hash. Spans keyed on the same ledger therefore land in one trace, and spans for different ledgers stay apart, with nothing passed between the threads. The spans appear as siblings rather than as a chain, which is the honest shape: none directly causes another, and their order varies with the path the ledger took through the node.

**Scope:** per node — measured on and specific to this individual server. Each node builds its own trace for the same ledger.

**See also:** [Ledger acquire (inbound fetch)](#ledger-acquire-inbound-fetch) · [Validation status](#validation-status) · [Time to first validated ledger](#time-to-first-validated-ledger) · [Following ONE slow ledger as a single trace](./telemetry-runbook.md#following-one-slow-ledger-as-a-single-trace)

<a id="validation-status"></a>

### Validation status

What this node's validation store did with an arriving validation. Only one result — current, meaning the validation is new and usable — counts toward accepting a ledger; the rest mean the validation was recorded and then counted for nothing, because it was stale, carried a sequence number that violates the increasing-sequence rule, or duplicated or contradicted another validation from the same validator. The distinction is what separates a node that is slow to validate from one that never will: both receive validations continuously, and without this split the two are indistinguishable. A companion flag says whether the validation actually reached the acceptance gate, since a validation arriving while another thread is already accepting the same ledger is set aside rather than acted on — which is why a trace can show a validation with no acceptance after it.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Validation quorum](#validation-quorum) · [Quorum shortfall](#quorum-shortfall) · [Per-ledger trace join](#per-ledger-trace-join) · [Negative UNL and validation quorum on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/negative-unl)

<a id="unl-fetch-outcome"></a>

### UNL fetch outcome

The result of retrieving a validator list from one configured UNL site and applying it. Accepted is the only success; same-sequence and known-sequence are normal no-op refreshes of a list the node already holds; fetch, status and parse errors are transport or content faults; and expired, stale, untrusted, invalid or unsupported-version mean the list arrived but was rejected, so no trusted keys are loaded from that site.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [UNL quorum headroom](#unl-quorum-headroom) · [UNL (Unique Node List)](#unl-unique-node-list) · [UNL (Unique Node List) on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/unl)

<a id="unl-quorum-headroom"></a>

### UNL quorum headroom

The trusted UNL key count minus the quorum a ledger needs, so it reads as spare validator keys. At or below zero — including a trusted key count of zero, meaning no usable list loaded — the node can track ledgers but can never declare one validated, however healthy the rest of the sync pipeline looks. A UNL site that keeps failing to fetch is the usual cause.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [UNL fetch outcome](#unl-fetch-outcome) · [Validation quorum](#validation-quorum) · [Validation quorum on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/negative-unl)

<a id="worker-pool-saturation"></a>

### Worker-pool saturation

The share of the job-queue worker threads currently executing a job. The thread count is fixed at startup from configuration, node size and hardware concurrency, so it is a real ceiling rather than an elastic one. Saturation is read together with the total number of jobs queued, because the ratio alone is ambiguous: every thread busy with nothing queued is a busy instant, while every thread busy with work piling up behind them is an exhausted pool. The distinction is what makes this a pool-level signal — when the pool is exhausted, every subsystem whose jobs are queued behind it slows at the same time, so each one appears to have its own independent fault. Reading saturation first attributes that whole pattern once, instead of once per victim.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Deferred job](#deferred-job) · [Job queue occupancy](#job-queue-occupancy) · [Server stall](#server-stall)

<a id="quorum-shortfall"></a>

### Quorum shortfall

A candidate ledger being refused the status of validated because the agreeing trusted validations counted for it fell short of the quorum it needed. It is the last gate of the sync pipeline and the one that can fail while everything upstream looks healthy: the node can hold every ledger it needs, apply them all, and still never declare one validated. Two shapes mean opposite things. A tally accumulating toward the quorum is slow and will get there, so the shortfall is transient. A tally that plateaus below the quorum is stuck, and the causes are upstream of ledger acquisition entirely — too few trusted validators reachable, or a validator-list or negative-UNL configuration that excludes the ones that are — so nothing in acquisition can fix it. A shortfall is also expected briefly on every healthy round, because the gate is first evaluated the moment this node finishes building a ledger, before its peers' validations for that ledger have arrived, and is retried as they come in. That makes the bare occurrence of a shortfall uninformative; only its persistence alongside a flat tally is a fault.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Validation quorum](#validation-quorum) · [UNL quorum headroom](#unl-quorum-headroom) · [Validated ledger](#validated-ledger) · [Time to first validated ledger](#time-to-first-validated-ledger) · [Negative UNL on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/negative-unl)

<a id="publish-lag"></a>

### Publish lag

The number of ledgers a node has fully validated but not yet published to its clients and subscribers. Publication trails validation by design, so a small lag that drains each round is the normal state; the diagnostic reading is a lag that stays positive or grows. That is a distinct fault from anything the quorum or acquisition signals describe: validation is working, the node itself is current, and only the pipeline that hands finished ledgers to subscribers is behind — so the visible symptom is stale data for API clients on a server that is not itself behind the network. Because it is local processing rather than peer supply that falls behind, the causes sit in job-queue starvation and main-loop stalls. One reading trap: a lag of zero is only healthy on a node that is validating. On a node that never has, the zero means there is nothing validated to publish at all, and the quorum gate is the thing to read instead.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Published ledger](#published-ledger) · [Validated ledger](#validated-ledger) · [Quorum shortfall](#quorum-shortfall) · [Deferred job](#deferred-job) · [Worker-pool saturation](#worker-pool-saturation)

<a id="time-to-first-validated-ledger"></a>

### Time to first validated ledger

The elapsed time from process start until the node first declared a ledger fully validated. Like the time to first reaching the full server state, it is a one-shot measurement: it is set the first time the quorum gate passes and never changes afterwards, so it has no trend to read. That leaves exactly two meaningful readings — a duration, meaning the node got there and this is how long it took, or zero, meaning it never has. The zero is the diagnostic signal rather than absent data. Its value is in the pairing: a duration for reaching the full server state beside a zero here means the node reached that state but has still never fully validated a ledger, which places the fault at the quorum gate rather than in ledger acquisition.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Time to first FULL](#time-to-first-full) · [Quorum shortfall](#quorum-shortfall) · [Validated ledger](#validated-ledger) · [Operating mode / server state](#operating-mode-server-state)

<a id="cat-peer-overlay-networking"></a>

## Peer & Overlay Networking

<a id="cluster"></a>

### Cluster

A cluster is a set of servers run by the same operator that trust each other, exchanging load and status directly and skipping some redundant checks. Cluster overhead is routine; sustained high cluster overhead suggests frequent cluster-state churn.

**Scope:** cluster-wide — shared across a co-operated cluster of nodes run by one operator.

**See also:** [Cluster on xrpl.org](https://xrpl.org/docs/concepts/networks-and-servers/clustering)

<a id="disconnect-reason"></a>

### Disconnect reason

The cause recorded when a peer connection is torn down, kept alongside the direction the connection was originally opened in. A single disconnect count cannot separate the two situations that matter, because they produce the same number: a node shedding load, which drops peers deliberately because it could not keep up with what it owed them or because a peer exceeded its resource allowance, and a network or topology fault, where the peer became unreachable, stopped answering keepalives, or turned out to be following a different chain. The first is a local capacity problem and the peer list is not the fix; the second is the opposite. A third group is neither — clean teardown at shutdown and peers closing their own side are ordinary churn, and a count dominated by those is healthy. The direction matters separately, since churn among the peers a node dials points somewhere different from churn among the peers that dial it.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Resource disconnect](#resource-disconnect) · [Slot census](#slot-census) · [Insane / diverged peers](#insane-diverged-peers)

<a id="fetch-pack"></a>

### Fetch-pack

A fetch-pack is a bulk transfer of ledger data used to accelerate catch-up across a range of ledgers. Fetch-pack traffic rises sharply while catching up and is near zero when fully synced; continuous fetch-pack traffic means the node never fully catches up.

**Scope:** per node — measured on and specific to this individual server.

<a id="getobject-object-fetch"></a>

### GetObject / object fetch

GetObject messages fetch individual pieces of ledger data from peers, broken down by object type: ledger headers, individual transactions, transaction-tree nodes, and state-tree nodes. Many messages carrying few bytes means small piecemeal fetches; few large messages means batch transfers.

**Scope:** per node — measured on and specific to this individual server.

<a id="have-requested-transactions"></a>

### Have / requested transactions

Have-transaction messages advertise that a peer holds particular transactions; requested-transaction messages ask for them. Comparing requested versus have gauges how well transactions are propagating; requested far exceeding have means peers are behind on propagation.

**Scope:** per node — measured on and specific to this individual server.

<a id="insane-diverged-peers"></a>

### Insane / diverged peers

Diverged (insane) peers are connected peers whose reported ledger state does not match the network's. Zero is healthy; a persistent nonzero count can indicate peers on a fork or misbehaving peers.

**Scope:** per node — measured on and specific to this individual server.

<a id="ledger-tree-nodes"></a>

### Ledger tree nodes

A ledger's contents are stored as Merkle trees (SHAMaps): a transaction tree and an account-state tree, each built from tree nodes. Peers fetch individual tree nodes (tx-node, account-state-node) to reconstruct a ledger. Account-state-node traffic dominates during state sync; transaction-set-candidate traffic dominates during consensus catch-up.

**Scope:** network-wide — a protocol-shared value, the same across all nodes.

<a id="manifest"></a>

### Manifest

A manifest is a validator's signed statement linking its long-term master key to its current ephemeral signing key, letting it rotate keys without losing trust. Manifest overhead rises around key rotations; sustained high manifest traffic suggests frequent reissue.

**Scope:** network-wide — a protocol-shared value, the same across all nodes.

<a id="overlay"></a>

### Overlay

The overlay is xrpld's peer-to-peer messaging layer connecting nodes. All inter-node traffic (transactions, proposals, validations, ledger data, control messages) flows over it, grouped into traffic categories for accounting.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Overlay on xrpl.org](https://xrpl.org/docs/concepts/networks-and-servers/peer-protocol)

<a id="proof-path"></a>

### Proof path

Proof-path request/response messages let a peer verify an individual ledger entry via its Merkle proof, without downloading the entire ledger. Volume rises when peers verify specific state, often during catch-up.

**Scope:** per node — measured on and specific to this individual server.

<a id="reduce-relay"></a>

### Reduce-relay

Reduce-relay lowers overlay bandwidth by forwarding proposals/validations through a chosen subset of peers (selected), suppressing the rest, while some older peers have the feature not-enabled. When suppression hides a message a peer needed, it fetches it on demand; a rising missing-tx rate means suppression is too aggressive.

**Scope:** per node — measured on and specific to this individual server.

<a id="replay-delta"></a>

### Replay delta

Replay-delta request/response messages transfer only the state changes between ledgers so a peer can efficiently replay them during catch-up, rather than refetching whole ledgers. Continuous replay traffic means the node is repeatedly replaying rather than staying current.

**Scope:** per node — measured on and specific to this individual server.

<a id="resource-charge"></a>

### Resource charge

The resource manager bills each peer a load cost per request, so expensive requests cost the sender more than cheap ones. For object fetches the charge scales with how many objects were asked for and how many of those were misses, with a surcharge once the request crosses a size band. A running balance above the warning threshold marks the peer as overactive; above the drop threshold the node sheds it. Requests inside the free allowance carry no charge beyond the flat per-message cost.

**Scope:** per node — measured on and specific to this individual server.

<a id="resource-disconnect"></a>

### Resource disconnect

The resource manager tracks each peer's load and disconnects those exceeding limits. A rising resource-disconnect count is consistent with abusive or misbehaving peers being shed as backpressure; a flat line is healthy.

**Scope:** per node — measured on and specific to this individual server.

<a id="set-get-share"></a>

### Set get/share

Set-get (fetch) and set-share messages exchange transaction-set data between peers as they reconcile which transactions belong in the closing ledger. Some exchange each ledger is normal; high set-get means peers are frequently missing transaction sets.

**Scope:** per node — measured on and specific to this individual server.

<a id="serve-refusal"></a>

### Serve refusal

A peer data request that this node declined to answer — the supply side of the sync exchange, as opposed to everything a node measures about its own fetching. It matters because a node that refuses everything it is asked for looks, from the outside, exactly like a node nobody asks: both serve nothing. From the asking peer's point of view a refusal is indistinguishable from a peer that does not hold the data, so refusals directly slow the sync of every peer that depends on this node. The reason divides them into two kinds. Self-inflicted refusals mean the node was too loaded to answer — its outgoing queue to that peer had grown past its limit, or the local fee track showed it under load, or too much bulk-transfer work was already queued — and these are the serving-side symptom of the same overload that shows up as stalls and job-queue backlog locally. A refusal because the data was simply not held is different: that is a genuine history gap, a question of what this node retains rather than how busy it is.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Fetch-pack](#fetch-pack) · [GetObject / object fetch](#getobject-object-fetch) · [Complete ledger ranges](#complete-ledger-ranges) · [Peer ledger supply](#peer-ledger-supply)

<a id="slot-census"></a>

### Slot census

A single consistent reading of everything PeerFinder knows about this node's peering position: how many outbound and inbound slots are occupied against how many exist, how many outbound attempts are in flight, how many configured fixed peers are connected against how many were configured, and the depth of the two address stores. Taken together at one instant, so the numbers can be compared against each other. The three terms worth defining plainly: an occupied outbound slot is a peer this node dialled and is now connected to; the bootstrap address store is a persisted list of addresses kept across restarts purely so a starting node has somewhere to dial; and the live address store holds addresses learned from peers during this session and exists only in memory. Occupancy alone cannot explain a peering failure, which is the reason the census exists. A node with no outbound peers might be dialling continuously and never completing, or not dialling at all because it has no addresses to try, or dialling only configured peers that are unreachable — three different faults with three different fixes, and the occupancy count is identical in all of them. It is the attempt count, the address-store depths, and the configured-versus-connected comparison that tell them apart.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Overlay](#overlay) · [Disconnect reason](#disconnect-reason) · [Peer ledger supply](#peer-ledger-supply) · [DNS resolve](#dns-resolve) · [Outbound dial latency](#outbound-dial-latency)

<a id="squelch"></a>

### Squelch

Squelching is a relay-control mechanism: a node tells peers to stop sending it a particular validator's messages when it already has a good source, reducing redundant forwarding. High suppressed counts mean squelch is saving bandwidth; ignored directives (peers not honoring squelch) should stay low.

**Scope:** per node — measured on and specific to this individual server.

<a id="trusted-untrusted-duplicate"></a>

### Trusted / untrusted / duplicate

Proposals and validations are trusted if they come from validators on this node's UNL, untrusted otherwise; duplicates are messages the node already received and discarded. High untrusted volume can indicate trusted-list misconfiguration or spam; high duplicates indicate inefficient relay.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Trusted / untrusted / duplicate on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/unl)

<a id="validator-list"></a>

### Validator list

Validator lists are the signed, published sets of recommended validators (the basis for a node's UNL). Peers exchange them so nodes stay configured with a current trusted set. Traffic bursts when lists update or new peers connect and is otherwise quiet.

**Scope:** network-wide — a protocol-shared value, the same across all nodes.

**See also:** [Validator list on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/unl)

<a id="cat-storage-internals"></a>

## Storage Internals

<a id="caches"></a>

### Caches (SLE / Ledger / TreeNode / FullBelow / AcceptedLedger)

xrpld keeps several caches: SLE (ledger entries), Ledger, AcceptedLedger, TreeNode, and the FullBelowCache (subtrees known to be fully present locally). A high hit rate means lookups are served from memory; low or falling hit rates indicate cache thrashing and extra back-end reads.

**Scope:** per node — measured on and specific to this individual server.

<a id="nodestore"></a>

### NodeStore

The NodeStore is xrpld's content-addressed object database holding all ledger tree nodes, keyed by hash. It is the main on-disk store read during queries and sync and written as new ledgers are stored; NuDB is the default backend.

**Scope:** per node — measured on and specific to this individual server.

<a id="nodestore-lookup-hit-miss"></a>

### NodeStore lookup (hit / miss)

A lookup is one attempt to fetch an object from the NodeStore by its hash. A hit is usually served from an in-memory cache and is cheap; a miss goes to the back-end store and costs a disk seek, so it is far more expensive. The hit/miss mix is therefore the main reason lookup time moves: a rising miss share explains slower lookups without any regression in the storage layer, while slower lookups on a hit-heavy mix point at the storage layer itself.

**Scope:** per node — measured on and specific to this individual server.

<a id="nudb"></a>

### NuDB

NuDB is a fast append-only key-value store used as the NodeStore backend. Its on-disk size grows steadily with retained ledger history; the growth slope is the data growth rate.

**Scope:** per node — measured on and specific to this individual server.

<a id="read-threads-read-queue-write-load"></a>

### Read threads / read queue / write load

The NodeStore serves reads through a pool of read threads (optionally bundling reads) fronted by a read queue, while writes are scored as write load. Read threads pinned at the maximum, or a high read queue, mean read I/O is saturated; high write load means back-end write pressure.

**Scope:** per node — measured on and specific to this individual server.

<a id="cat-validator-health"></a>

## Validator Health

<a id="amendment-block-countdown"></a>

### Amendment block countdown

The window between an amendment this build does not understand reaching majority among validators and that amendment actually activating. It exists because amendment activation is not instantaneous: once an unsupported amendment has majority support it becomes expected to activate at a known future time, and until then the node still works normally. That window is the only actionable part of an otherwise terminal condition — after activation the node stops validating and cannot resume without a software upgrade, so there is no operational fix left, only a rebuild and restart. Read as a countdown it therefore outranks every other sync signal in urgency: a node counting down is going to stop validating at a knowable moment, and anything else that looks wrong is secondary. The healthy state is reported as an explicit sentinel value rather than as absent data, so a node with nothing pending is distinguishable from a node whose reporting has broken, and the countdown is held at zero rather than going negative once activation is due. The identity of the blocking amendment is deliberately not carried on the metric — the network can vote on any amendment identifier, including ones this build has never heard of, which would make it an unbounded label — so the hash comes from the log line that records the amendment reaching majority.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Amendment blocked](#amendment-blocked) · [UNL blocked](#unl-blocked) · [Amendments on xrpl.org](https://xrpl.org/docs/concepts/networks-and-servers/amendments)

<a id="amendment-blocked"></a>

### Amendment blocked

Amendments are protocol changes activated by validator voting. If the network enables an amendment a node's build does not understand, the node becomes amendment-blocked: it stops processing to avoid diverging, and requires a software upgrade. OK is healthy; BLOCKED means an upgrade is needed.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Amendment blocked on xrpl.org](https://xrpl.org/docs/concepts/networks-and-servers/amendments)

<a id="ledgers-closed-rate"></a>

### Ledgers closed rate

The rate at which this node closes ledgers, roughly 12-20 per minute on Mainnet (one per ~3-5s close). It should match the network's steady cadence; a drop toward zero means the node stopped participating.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Ledgers closed rate on xrpl.org](https://xrpl.org/docs/concepts/ledgers/ledger-close-times)

<a id="unl-unique-node-list"></a>

### UNL (Unique Node List)

A node's Unique Node List is the set of validators it trusts not to collude. The node reaches consensus by listening to its UNL and declaring a ledger validated when a quorum of them agree. UNL expiry (days left) matters because an expired list leaves the node without a trusted set.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [UNL (Unique Node List) on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/unl)

<a id="unl-blocked"></a>

### UNL blocked

UNL-blocked means the node could not establish a valid trusted validator list (for example, all configured lists expired or failed to load), so validator trust cannot be established. OK is healthy; BLOCKED halts safe participation.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [UNL blocked on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol)

<a id="validation-agreement"></a>

### Validation agreement

Validation agreement is the fraction of recent ledgers where this validator issued a validation matching the consensus outcome (agreed) rather than missing or disagreeing (missed). A sustained dip signals configuration drift, unreliability, or a network partition.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Validation agreement on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/consensus-structure#validation)

<a id="validation-quorum"></a>

### Validation quorum

The quorum is the minimum count of trusted-validator validations that must agree before a server declares a ledger validated (by default about 80% of the UNL). It is derived from the active validator list and changes when that list changes.

**Scope:** network-wide — a protocol-shared value, the same across all nodes.

**See also:** [Validation quorum on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/negative-unl)

<a id="validations-checked-vs-sent"></a>

### Validations checked vs sent

Checked validations are those received from other validators and verified by this node (reflecting network validation traffic reaching it); sent validations are those this node issues (roughly one per closed ledger for a validator).

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Validations checked vs sent on xrpl.org](https://xrpl.org/docs/concepts/consensus-protocol/consensus-structure#validation)

<a id="cat-rpc-pathfinding"></a>

## RPC & Pathfinding

<a id="batch-vs-single-rpc"></a>

### Batch vs single RPC

A single request carries one command; a batch request bundles several. Single requests usually dominate; a batch rate climbing sharply is consistent with bulk automation or amplification attempts.

**Scope:** per node — measured on and specific to this individual server.

<a id="clio-reporting-client"></a>

### Clio / reporting client

Clio is a separate reporting server that offloads historical and API queries from xrpld, fetching data via the gRPC interface. gRPC panels are nonzero mainly when reporting/Clio-style clients are connected.

**Scope:** per node — measured on and specific to this individual server.

<a id="grpc"></a>

### gRPC

gRPC is a high-performance binary RPC protocol the node exposes for specific consumers, chiefly reporting-mode (Clio) clients. gRPC traffic is zero on nodes without such clients; its status is only ever success or error.

**Scope:** per node — measured on and specific to this individual server.

<a id="order-book"></a>

### Order book

An order book holds the standing offers to exchange a given currency pair in the XRP Ledger's decentralized exchange. Pathfinding and some RPC queries walk order books; cost grows with order-book depth and request complexity.

**Scope:** network-wide — a protocol-shared value, the same across all nodes.

**See also:** [Order book on xrpl.org](https://xrpl.org/docs/concepts/tokens/decentralized-exchange)

<a id="path-request-discovery"></a>

### Path request / discovery

A path request is a client subscription for payment paths; discovery passes are the periodic refreshes the node runs to keep those paths current as ledgers close. Discovery cost tracks request demand and is a cost driver for subscription-heavy nodes.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Path request / discovery on xrpl.org](https://xrpl.org/docs/references/http-websocket-apis/public-api-methods/path-and-order-book-methods/path_find)

<a id="pathfinding-fast-full"></a>

### Pathfinding (fast / full)

Pathfinding searches for routes along which a cross-currency payment can flow through order books and AMMs. A fast search trades accuracy for speed; a full search is exhaustive and much more expensive. Sustained high durations indicate pathfinding-heavy clients straining the node.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Pathfinding (fast / full) on xrpl.org](https://xrpl.org/docs/concepts/tokens/fungible-tokens/paths)

<a id="resource-drops-warnings"></a>

### Resource drops / warnings

The resource manager meters each peer and client; it first warns endpoints for excessive usage and then drops or blocks them. Nonzero rates mean the node is actively rejecting abusive connections; zero is expected when no abusive consumers are present.

**Scope:** per node — measured on and specific to this individual server.

<a id="rpc-command-method"></a>

### RPC command / method

Clients call the node via named RPC commands (also called methods), such as account_info, ledger, or submit, over HTTP, WebSocket, or gRPC. Panels break rate, latency, and errors down by command; heavy commands (ledger/account queries) cost far more than status calls.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [RPC command / method on xrpl.org](https://xrpl.org/docs/references/http-websocket-apis/public-api-methods)

<a id="websocket"></a>

### WebSocket

WebSocket is a long-lived connection transport for the node's API, used by clients that subscribe to streams or send many requests. WebSocket message rate is nonzero only when clients use WebSocket; HTTP-only nodes read zero.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [WebSocket on xrpl.org](https://xrpl.org/docs/references/http-websocket-apis/api-conventions)
