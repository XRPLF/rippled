# `FeeVote.h` — Validator Fee-Voting Interface

`FeeVote.h` defines the pure abstract interface through which a validator node participates in the XRPL's on-ledger fee governance mechanism. The XRP Ledger has no central authority to adjust network fees; instead, validators reach consensus on desired fee levels by embedding preferences in their validation messages and, at periodic "flag ledgers," injecting pseudo-transactions that move the live fee schedule closer to the collective preference.

## The Two-Phase Protocol

Fee governance works in two distinct phases, each corresponding to one interface method.

**Phase 1 — `doValidation()`** runs whenever this node produces a validation for a closed ledger. It receives the fee schedule currently in effect (`Fees const& lastFees`), the active rules set (to detect the `featureXRPFees` amendment), and the `STValidation` object being constructed. If any of this node's target fees (`reference_fee`, `account_reserve`, `owner_reserve` from its `FeeSetup` configuration) differ from the current on-ledger values, the method stamps the desired values into the validation's serialized fields. This is gossip: the node broadcasts its preferences to every other trusted validator so that votes can be tallied later. No fee changes occur at this step.

**Phase 2 — `doVoting()`** runs only at flag ledgers (every 256 ledgers, detected via `isFlagLedger()`). At this point the node has collected a `parentValidations` vector containing recent `STValidation` objects from trusted peers. The implementation tallies each field — base fee, base reserve, and increment reserve — using a `detail::VotableValue` tally object per field, then calls `getVotes()` to determine the winning value. If any winning value differs from the current on-ledger setting, a `ttFEE` pseudo-transaction is serialized and inserted into `initialPosition` (a `SHAMap` representing the candidate next ledger). If other validators agree, they will have injected the same transaction, and it will survive consensus.

## Design Decisions

**Interface-only header, factory construction.** The header deliberately exposes only the abstract `FeeVote` class and the `make_FeeVote()` factory. The concrete `FeeVoteImpl` lives entirely in `FeeVoteImpl.cpp`. This is not incidental: callers depend only on the interface, making the voting logic substitutable for tests and keeping build dependencies minimal. The forward-declared `struct FeeSetup` keeps `Config.h` out of this header's transitive include chain.

**Conservative vote resolution.** `VotableValue::getVotes()` only considers votes that fall *between* the current value and the operator's target. The winning value is whichever receives the most votes within that range. This prevents any single minority of validators from pushing fees past the point where the network's median preference lies. Fees change incrementally over multiple flag ledgers rather than jumping in a single step.

**Amendment-aware field handling.** The `featureXRPFees` amendment changed the wire representation of fee fields from legacy integers (`sfBaseFee` as `uint64`, `sfReserveBase`/`sfReserveIncrement` as `uint32`) to native XRP drop amounts (`sfBaseFeeDrops`, `sfReserveBaseDrops`, `sfReserveIncrementDrops`). Both `doValidation()` and `doVoting()` branch on `rules.enabled(featureXRPFees)` to read and write the correct field types. For the legacy path, the implementation performs safe narrowing checks (`dropsAs<uint32_t>()`) and treats out-of-range or non-native values as abstentions (`noVote()`), rather than throwing, because validation field values originate from external, untrusted network peers.

**Trusted-only tallying.** In `doVoting()`, the loop over `parentValidations` skips any validation that is not marked trusted via `val->isTrusted()`. This ensures that Sybil validators or unknown nodes cannot artificially skew fee votes.

## Relationship to `FeeSetup` and `Config`

`FeeSetup` (defined in `Config.h`) represents the *operator's preference* — the fees they wish the network to eventually converge on. Its defaults are `reference_fee = 10` drops, `account_reserve = 10 XRP`, and `owner_reserve` per item. These are loaded from the node's config file via `setup_FeeVote()` and passed to `make_FeeVote()` at startup. The resulting `FeeVote` object is owned by the application layer and invoked by the consensus machinery at the appropriate moments.

The interface's two-parameter split — `doValidation` operating on `Fees` + `Rules` rather than a full `ReadView`, and `doVoting` receiving the complete `ReadView` — reflects their different needs: validation stamping only needs the current fee values and amendment rules, while vote tallying needs access to the full ledger context including sequence number and `SHAMap` manipulation.