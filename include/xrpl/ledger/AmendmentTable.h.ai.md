# `include/xrpl/ledger/AmendmentTable.h`

## Role in the System

`AmendmentTable` is the central interface for XRPL's on-chain governance mechanism. The XRP Ledger evolves through *amendments* — protocol changes that require supermajority validator approval before they activate. This header defines the abstract contract that governs the full lifecycle of those amendments: registration, voting, activation, and the critical safety valve of detecting when a node is running software that lacks support for an amendment the network has already enabled (the "amendment blocked" condition).

The class deliberately separates the interface layer (this header) from the ledger-reading infrastructure. This decoupling is explicitly called out in the source: the concrete `doVoting` and `doValidatedLedger` methods declared here exist specifically so the implementation does not need to import ledger-level types like `ReadView` or `SHAMap`. The comment in the file reads: "These APIs will merge when the view code supports a full ledger API" — an honest acknowledgment of an architectural seam that has not yet been closed.

## The `FeatureInfo` Inner Struct

`FeatureInfo` bundles the three things the table needs to know about each registered amendment: its human-readable `name`, its canonical 256-bit `feature` hash (the `uint256` used everywhere in the ledger), and its `VoteBehavior`. The `VoteBehavior` enum (defined in `Feature.h`) can be `DefaultNo`, `DefaultYes`, or `Obsolete`. This last value is subtle: obsolete amendments are still registered so that nodes running this software don't become amendment-blocked if those amendments get enabled, but the node will not emit votes for them.

`FeatureInfo` is non-default-constructible by design — each instance must carry all three fields.

## Two-Layer API Design

The interface uses a two-layer pattern. The pure virtual methods form the internal amendment-table API that the implementation satisfies. Sitting on top of these are two concrete non-virtual methods — `doValidatedLedger(std::shared_ptr<ReadView const>)` and `doVoting(std::shared_ptr<ReadView const>, ...)` — that extract ledger state by calling `getEnabledAmendments()` and `getMajorityAmendments()` (both declared in `View.h`), then delegate to the implementation's pure-virtual overloads.

`getEnabledAmendments()` returns the set of `uint256` amendment IDs currently active in a ledger. `getMajorityAmendments()` returns a `majorityAmendments_t` — a `std::map<uint256, NetClock::time_point>` associating each amendment that has crossed the validator majority threshold with the time it first achieved that majority. The time point is what drives the two-week activation window: if majority is held continuously until `closeTime >= majorityTime + firstMajorityTime`, the amendment is enabled.

This adapter pattern keeps the implementation independent of the ledger view layer. The table's internal `doVoting` takes pre-extracted `std::set<uint256>` and `majorityAmendments_t`; the public `doVoting` does the extraction itself from a `ReadView`.

## The Voting Pipeline

When consensus is building the initial transaction set for a new ledger, it calls the public `doVoting`. That method:

1. Delegates to the abstract `doVoting` to compute a `std::map<uint256, std::uint32_t>` of actions, where the key is an amendment ID and the value is a flags field (zero means "enable this amendment").
2. For each action, constructs an `STTx` of type `ttAMENDMENT` — a pseudo-transaction that carries the amendment ID and target ledger sequence. These are not signed user transactions; they are injected directly by the consensus engine.
3. Adds each pseudo-transaction to the `initialPosition` `SHAMap` under `tnTRANSACTION_NM` node type.

This is how amendments enter the ledger: they become pseudo-transactions in the consensus-agreed transaction set, which validators then process as part of closing the flag ledger.

The companion `doValidation` method runs in the opposite direction — it's called when a validator is building its `STValidation` message, and it returns the list of amendment IDs the node wishes to vote for, which the consensus layer then embeds in the outgoing validation.

## Amendment Blocking Detection

Two methods guard against running unsupported code: `hasUnsupportedEnabled()` returns `true` if any amendment active on the network is not present in the node's supported list. `firstUnsupportedExpected()` returns an `optional<NetClock::time_point>` representing when the first such amendment is projected to reach its activation window. Together they allow the application layer to warn operators and, eventually, halt participation before the node starts misprocessing ledgers.

## `needValidatedLedger` and State Efficiency

`needValidatedLedger(LedgerIndex seq)` exists as an optimization gate. The abstract `doValidatedLedger(LedgerIndex, std::set<uint256>, majorityAmendments_t)` only needs to run when something relevant could have changed — primarily at flag ledgers (multiples of 256 in XRPL). Calling `needValidatedLedger` first avoids the cost of extracting amendment state from every validated ledger when the overwhelming majority will have no effect on amendment voting outcomes.

## `trustChanged` and Anti-Flapping

`trustChanged(hash_set<PublicKey> const& allTrusted)` notifies the table whenever the UNL (Unique Node List) changes. The implementation's `TrustedVotes` class (in `AmendmentTable.cpp`) uses this to update its per-validator vote cache. Rather than counting only the votes present in the current round's validations, `TrustedVotes` retains the last known vote from each trusted validator with a 24-hour expiration. This prevents amendment vote "flapping" where a temporarily offline validator causes an amendment's apparent support to oscillate across the 80% supermajority threshold on successive flag ledgers.

## Factory Function

`make_AmendmentTable` (declared both here and repeated in `AmendmentTableImpl.h`) creates the concrete implementation. Its parameters reflect the complete configuration surface: a `ServiceRegistry` reference, the `majorityTime` duration (the length of sustained supermajority required before activation), the vector of `FeatureInfo` for all supported amendments, and two `Section` objects from the server config representing externally-forced enabled and vetoed amendments. The vetoed set allows node operators to withhold votes from specific amendments regardless of the node's compiled-in `VoteBehavior`.