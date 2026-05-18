# `LoadFeeTrack.cpp` — Dynamic Transaction Fee Scaling

This file implements the two methods of `LoadFeeTrack` that mutate fee state, and the free function `scaleFeeLoad` that converts a raw fee amount into the load-adjusted amount that a transaction must actually pay. Together they form the core of the XRPL server's adaptive fee mechanism, which dynamically prices transaction submission proportional to how stressed the local node is.

## Architecture and Purpose

The XRPL protocol requires every transaction to include a minimum fee in drops. Under normal conditions, that minimum is just the "reference fee" — a tiny amount. But a validator or relay node under heavy load has every incentive to charge more: higher fees make senders prioritise their most important transactions, shedding low-value traffic as the node approaches capacity. `LoadFeeTrack` (header in `include/xrpl/server/LoadFeeTrack.h`) tracks three independent scale factors:

- `localTxnLoadFee_` — this node's own load multiplier, adjusted each second
- `remoteTxnLoadFee_` — the highest multiplier seen from network peers (written externally via `setRemoteFee()`)
- `clusterTxnLoadFee_` — a multiplier from the trusted cluster (written via `setClusterFee()`)

All three start at `lftNormalFee = 256`. This constant acts as the denominator of the scale ratio: a fee of 10,000 drops with a factor of 256 / 256 = 1.0 costs exactly 10,000 drops. A factor of 512 doubles it.

## Fee Adjustment: `raiseLocalFee()` and `lowerLocalFee()`

`LoadManager` owns a dedicated background thread that wakes every second, checks whether the application's job queue is overloaded, and calls exactly one of these two methods. If the queue is overloaded, it calls `raiseLocalFee()`; otherwise `lowerLocalFee()`.

**`raiseLocalFee()`** has a deliberate two-sample guard: it pre-increments `raiseCount_` and returns `false` immediately if the new count is less than 2. Only on the second consecutive overloaded sample does it actually mutate the fee. This single-sample immunity prevents transient queue bursts from immediately penalising submitters — load must persist for at least two seconds before fees begin to climb. Once triggered, the fee is first snapped up to at least `remoteTxnLoadFee_` (ensuring the local rate never falls below the network's observed rate at the moment of raising), then increased by `1/4` of the current value — a compound, exponential escalation. An absolute ceiling of `lftFeeMax = lftNormalFee × 1,000,000` prevents the multiplier from growing unboundedly.

**`lowerLocalFee()`** resets `raiseCount_` to zero immediately, so any future raise will again require two consecutive overloaded samples. It then subtracts `1/4` of the current fee, mirroring the raise step size, but floors at `lftNormalFee` so the local multiplier never drops below baseline. Both functions return `false` when the fee did not actually change — either because it was already at the boundary or because the guard short-circuited — allowing `LoadManager` to skip the `reportFeeChange()` notification in that case.

The raise/lower fraction constants (`lftFeeIncFraction` and `lftFeeDecFraction`) are both 4, so the geometric step size is symmetric. However, because raising requires two consecutive triggers while lowering acts immediately, the fee decays faster than it escalates in practice — this is an asymmetric bias toward giving submitters relief, appropriate for a public network.

All mutations and reads go through `std::mutex lock_` via `std::lock_guard`, providing RAII-safe locking. The mutex is `mutable` so that `const` query methods like `getLocalFee()` and `getScalingFactors()` can still acquire it safely.

## Fee Computation: `scaleFeeLoad()`

`scaleFeeLoad()` is the point where an incoming or outgoing transaction's raw base fee is converted into what the node actually requires. Its signature accepts an `XRPAmount` (in drops), the `LoadFeeTrack` instance, a `Fees` struct (not directly used in the current implementation body), and a `bUnlimited` flag that marks trusted/privileged clients.

The function delegates to `getScalingFactors()`, which returns a pair: `(max(local, remote), max(remote, cluster))`. The first element — the effective local factor — is what drives scaling. The second — the "remote factor" — is used for the privilege exemption.

**Privilege logic**: Nodes in the trusted cluster or with administrator-level access receive `bUnlimited = true`. They pay only the remote/cluster rate as long as the local factor is below four times the remote. Once local stress exceeds that threshold — the node is genuinely overwhelmed — even privileged callers pay the full local rate. The threshold of 4× represents a judgment that moderate local overload should not punish trusted submitters, but severe overload affects everyone.

**Overflow-safe arithmetic**: The actual computation is `fee × feeFactor / lftNormalFee`, but since `feeFactor` can reach `lftFeeMax = 256,000,000` and `fee` can be a substantial XRP amount in drops, the intermediate product can overflow 64 bits. `mulDiv()` (from `include/xrpl/protocol/Units.h`) handles this by performing the multiplication in 128-bit via `boost::multiprecision::uint128_t` and returning `std::nullopt` on overflow. `scaleFeeLoad()` checks that optional and throws `std::overflow_error` via the XRPL `Throw<>` helper if it fires — a defensive hard failure rather than silently returning a wrong fee.

## Integration

`LoadManager` is the sole writer of local fee state; it runs the raise/lower loop in its dedicated thread. The transaction application path (`Transactor.cpp`) calls `scaleFeeLoad()` during fee checking, passing `tapUNLIMITED` flag through to `bUnlimited`. RPC signing code (`TransactionSign.cpp`) similarly calls `scaleFeeLoad()` to report what fee a transaction will require before submission. Changes to the fee track are broadcast via `app_.getOPs().reportFeeChange()`, which notifies the network layer to propagate updated fee information to peers.