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

<a id="cat-node-state-sync"></a>

## Node State & Sync

<a id="back-fill-catch-up"></a>

### Back-fill / catch-up

When a node is missing ledgers (at startup, after an outage, or to extend history) it back-fills by fetching them from peers. Elevated back-fill activity is normal while catching up and should fall to near zero once history is complete and the node is synced.

**Scope:** per node — measured on and specific to this individual server.

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

<a id="ledger-acquire-inbound-fetch"></a>

### Ledger acquire (inbound fetch)

Acquiring a ledger means requesting it and its contents from peers when the node lacks it. Acquire outcomes split into complete and failed; a rising failed rate means the node cannot fetch needed ledgers from its peers.

**Scope:** per node — measured on and specific to this individual server.

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

<a id="operating-mode-server-state"></a>

### Operating mode / server state

The server state describes how fully the node is participating, in ascending order: disconnected, connected, syncing, tracking, full (caught up), and for validators validating and proposing. A healthy non-validator sits in Full; frequent transitions out of Full indicate instability. Transitions are recorded as a from-to edge rather than a bare count, which is what distinguishes a clean one-way climb to Full from flapping in and out of it.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Time to first FULL](#time-to-first-full) · [Network ledger gate](#network-ledger-gate) · [Operating mode / server state on xrpl.org](https://xrpl.org/docs/references/http-websocket-apis/api-conventions/xrpld-server-states)

<a id="outbound-dial-latency"></a>

### Outbound dial latency

The elapsed time of one outbound peer connection attempt, from starting the TCP connect through TLS to the protocol upgrade, measured to whichever outcome ends it. Every attempt ends in exactly one outcome, so the outcome names the stage that broke; because the timing covers failures too, a value pinned near the dial timeout means peers accept the connection but never finish the handshake.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [DNS resolve](#dns-resolve) · [Handshake negotiation failure](#handshake-negotiation-failure) · [Peer protocol on xrpl.org](https://xrpl.org/docs/concepts/networks-and-servers/peer-protocol)

<a id="received-data-stash"></a>

### Received-data stash

Peer packets held for later processing because a ledger acquire cannot apply them as fast as they arrive. A shallow stash means node data is applied as it lands. A growing stash means the bottleneck is local processing — job-queue depth or disk latency — rather than peer supply, which is the opposite conclusion from an acquire that receives nothing at all. Read alongside the in-flight acquire count, since an empty stash on an idle node says nothing about acquire health.

**Scope:** per node — measured on and specific to this individual server.

**See also:** [Add-node outcome](#add-node-outcome) · [Acquire stall](#acquire-stall)

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

<a id="cat-peer-overlay-networking"></a>

## Peer & Overlay Networking

<a id="cluster"></a>

### Cluster

A cluster is a set of servers run by the same operator that trust each other, exchanging load and status directly and skipping some redundant checks. Cluster overhead is routine; sustained high cluster overhead suggests frequent cluster-state churn.

**Scope:** cluster-wide — shared across a co-operated cluster of nodes run by one operator.

**See also:** [Cluster on xrpl.org](https://xrpl.org/docs/concepts/networks-and-servers/clustering)

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

<a id="resource-disconnect"></a>

### Resource disconnect

The resource manager tracks each peer's load and disconnects those exceeding limits. A rising resource-disconnect count is consistent with abusive or misbehaving peers being shed as backpressure; a flat line is healthy.

**Scope:** per node — measured on and specific to this individual server.

<a id="set-get-share"></a>

### Set get/share

Set-get (fetch) and set-share messages exchange transaction-set data between peers as they reconcile which transactions belong in the closing ledger. Some exchange each ledger is normal; high set-get means peers are frequently missing transaction sets.

**Scope:** per node — measured on and specific to this individual server.

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
