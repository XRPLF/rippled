# `RCLCxTx.h` — Transaction Adapter Types for RCL Consensus

This header defines the two adapter types — `RCLCxTx` and `RCLTxSet` — that bridge the XRP Ledger's `SHAMap`-based transaction storage to the generic `Consensus<Adaptor>` template engine. It is part of the glue layer in `src/xrpld/app/consensus/` whose sole purpose, as its README states, is "connecting the generic consensus algorithm to the xrpld-specific instance of consensus."

## The Adapter Contract

The generic `Consensus<Adaptor>` template (found in `src/xrpld/consensus/Consensus.h`) is policy-based: it drives the consensus round but knows nothing about ledgers, nodes, or serialization. It derives its transaction set type as `Adaptor::TxSet_t` and then requires that type to expose a specific structural contract — a nested `Tx` type, a nested `MutableTxSet` class, an `ID` alias, and the methods `exists()`, `find()`, `id()`, and `compare()`. `RCLCxTx` and `RCLTxSet` together satisfy that contract for the live XRP Ledger Consensus (RCL), with `RCLConsensus::Adaptor` declaring `using TxSet_t = RCLTxSet`.

## `RCLCxTx`: A Transaction Handle

`RCLCxTx` is a minimal value type wrapping a `boost::intrusive_ptr<SHAMapItem const>`. It adds nothing except the `ID` type alias (`uint256`) and the `id()` accessor that delegates to `SHAMapItem::key()`. The intrusive pointer's reference counting is baked into `SHAMapItem` itself, making copies cheap compared to `shared_ptr`. This class is what `DisputedTx<RCLCxTx, NodeID_t>` wraps during intra-round disagreements over which transactions belong in the next ledger.

## `RCLTxSet`: An Immutable Transaction Set View

`RCLTxSet` wraps a `std::shared_ptr<SHAMap>` and treats it as an immutable, content-addressed snapshot of the proposed transaction set. Its `id()` method returns `map_->getHash().as_uint256()`, which is the Merkle root of all included transactions — the value that validators exchange as their position during consensus voting.

### `find()` Returns a Pointer, Not a `Tx`

The most deliberate API quirk is `find()`: it returns `boost::intrusive_ptr<SHAMapItem const>` rather than an `RCLCxTx`. The in-code comment explains why — `RCLCxTx` cannot represent absence (it holds a non-nullable intrusive pointer), so returning a nullable pointer lets the generic consensus layer distinguish "found" from "not found" using standard pointer semantics before constructing a `Tx` value. This is a consequence of designing `RCLCxTx` as a non-optional handle rather than forcing optional semantics into the core type.

### Bounded `compare()` for Safety

`compare()` computes the symmetric difference between two transaction sets by delegating to `SHAMap::compare()` and translating the resulting `Delta` map into a `std::map<uint256, bool>`, where `true` means the transaction is present in `this` set. The call caps the comparison at 65,536 differences — a hard limit chosen to bound the CPU work a malicious or corrupted validator set could force on a node. The `XRPL_ASSERT` inside the loop enforces that any entry in the delta is exclusive to exactly one side, which is guaranteed by `SHAMap::compare()` semantics.

## `MutableTxSet`: Copy-on-Write Mutation

`MutableTxSet` is a nested friend class that provides the transient mutable phase when the consensus engine needs to adjust a transaction set (inserting or erasing individual transactions during dispute resolution). Its constructor calls `src.map_->snapShot(true)` — SHAMap's copy-on-write mechanism — cloning only the tree path nodes that change. Converting back to an immutable `RCLTxSet` via `RCLTxSet(MutableTxSet const&)` calls `snapShot(false)`, producing a new immutable view with a freshly computed Merkle root. This pattern means mutation never touches the original set, preserving referential integrity for other parts of the system that hold references to the prior snapshot.

The `insert()` method uses `SHAMapNodeType::tnTRANSACTION_NM` (non-metadata transaction node type), which is the correct leaf type for unsigned transaction entries in the SHAMap tree.

## Relationship to the Broader Consensus Layer

`RCLCxTx.h` is intentionally thin — it contains no application logic, no logging, and no network I/O. All of that lives in `RCLConsensus.cpp`, which uses these types to acquire transaction sets from peers (`acquireTxSet`), to apply the agreed set to the open ledger, and to share individual transactions (`share(RCLCxTx const&)`). The separation keeps the generic consensus algorithm's data model clean and testable independently of the full application stack.