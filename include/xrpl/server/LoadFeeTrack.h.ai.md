# `LoadFeeTrack.h` — Dynamic Transaction Fee Scaling

`LoadFeeTrack` is the node-local component responsible for tracking and adjusting the transaction fee multiplier in response to server load. It sits at the intersection of the consensus protocol, the job queue, and the RPC layer: every transaction that passes through rippled must be validated against the effective fee that this class computes.

## The Three-Factor Model

The XRPL fee system separates *base fee* (a ledger-consensus parameter stored in `Fees::base`) from a *load factor* that reflects how busy a particular server or cluster currently is. `LoadFeeTrack` manages three independent load-factor dimensions, each stored as a `uint32_t` scale value where `lftNormalFee = 256` represents the baseline (1× multiplier):

- **`localTxnLoadFee_`**: this server's own load pressure, driven by job-queue depth.
- **`remoteTxnLoadFee_`**: the load factor reported by connected peers, set externally via `setRemoteFee()`.
- **`clusterTxnLoadFee_`**: the highest fee reported within the server's validator cluster, set via `setClusterFee()`.

The effective fee presented to the network is `getLoadFactor()`, which returns `max(cluster, local, remote)`. This conservatively-pessimistic design ensures that even a lightly loaded node will not accept transactions at below-network rates when the broader network or cluster is under stress.

## Asymmetric Raise/Lower Mechanics and Hysteresis

`raiseLocalFee()` and `lowerLocalFee()` implement deliberate asymmetry. The raise path starts from `max(local, remote)` — it "catches up" to the remote factor before adding 25% (`localTxnLoadFee_ + localTxnLoadFee_ / 4`). Critically, it requires two consecutive raise calls before the fee actually increases: `raiseCount_` must reach 2 before the multiplier moves. This hysteresis prevents a single transient overload spike from immediately penalizing users. The fee is hard-capped at `lftNormalFee * 1,000,000` (256 million), which translates to roughly 1 million times the base fee.

The lower path is more direct: it always reduces by 25% toward the floor (`lftNormalFee`), and it resets `raiseCount_` to zero so a subsequent raise must again accumulate two ticks. The floor ensures the fee never drops below the ledger-consensus base rate regardless of how many lower operations are applied.

`LoadManager` drives this machinery once per second: it calls `raiseLocalFee()` when `JobQueue::isOverloaded()` is true, and `lowerLocalFee()` otherwise. When either function reports a change (returns `true`), `NetworkOPs::reportFeeChange()` is triggered to broadcast the updated fee to subscribers.

## The `scaleFeeLoad()` Free Function

The companion function `scaleFeeLoad(fee, feeTrack, fees, bUnlimited)` applies the load factor to a specific fee amount using integer arithmetic without overflow, via `mulDiv`:

```
scaled_fee = fee * feeFactor / lftNormalFee
```

`getScalingFactors()` returns two values: `feeFactor = max(local, remote)` and `uRemFee = max(remote, cluster)`. The split matters for the `bUnlimited` path. Privileged clients (internal RPC users, server operators) receive a discount: if `feeFactor > uRemFee` but `feeFactor < 4 * uRemFee`, the privileged client is charged only the network/cluster rate rather than the locally-elevated rate. The threshold of 4× represents an explicit policy choice — trusted users absorb normal network load pressure but are shielded from moderate local overload, only paying the full rate when the server is under extreme stress relative to the network.

## Thread Safety

All mutable state (`localTxnLoadFee_`, `remoteTxnLoadFee_`, `clusterTxnLoadFee_`, `raiseCount_`) is protected by a `mutable std::mutex`. The `const` getters lock the same mutex, making concurrent reads and writes safe without requiring the caller to hold any external lock. This is intentional: `LoadManager` modifies the local fee from its own thread while the RPC and overlay layers read it concurrently from their threads.

## Relationship to `Fees`

`Fees` (from `include/xrpl/protocol/Fees.h`) stores the ledger-consensus parameters: `base`, `reserve`, and `increment`. These are fixed per-ledger and do not change mid-ledger. `LoadFeeTrack` is orthogonal — it holds the *multiplier* that scales `Fees::base` to reflect current server conditions. `scaleFeeLoad()` brings them together: the input `fee` is typically derived from `Fees::base`, and the output is the actual drop cost a transaction must pay under current load.

The `isLoadedLocal()` and `isLoadedCluster()` predicates are used by higher layers (e.g., `RCLConsensus`, `LedgerCleaner`) to gate expensive operations or shed non-critical work when the server is under pressure, providing a back-pressure signal beyond pure fee elevation.