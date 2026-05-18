# `include/xrpl/protocol/Protocol.h` — XRPL Protocol Constants and Utilities

## Purpose and Role

`Protocol.h` is the canonical source of truth for every hard-coded numeric limit and protocol constant in the XRP Ledger. It exists as a deliberate single point of definition: any value that, if changed silently, would create a **hard fork** — a ledger state disagreement between nodes running different software versions — belongs here. The file's own doxygen note makes this explicit: altering these values without pairing them with an amendment-gated detection mechanism will split the network.

Because it is a header-only file (with the trivial exception of two small helper functions whose implementations live in `Protocol.cpp`), all constants are `constexpr`, available at compile time, and inlined everywhere they are used.

## Dependency Chain

The file includes three lightweight headers:
- `ByteUtilities.h` — provides `megabytes()` and `kilobytes()`, pure `constexpr` template functions used to express `txMaxSizeBytes` in human-readable units rather than the raw integer `1048576`.
- `base_uint.h` — brings in `uint256`, used as the typedef basis for `TxID`.
- `Units.h` — provides the `Bips<T>` and `TenthBips<T>` strongly-typed value wrappers, along with their concrete aliases (`Bips32`, `TenthBips16`, etc.) and arithmetic concepts. All fee-rate constants depend on these types.

## Transaction and Ledger Boundary Constants

The most fundamental constants are the transaction size bounds: `txMinSizeBytes = 32` and `txMaxSizeBytes = megabytes(1)`. These bound how much data a single transaction may carry on the wire; the lower bound prevents trivially malformed objects from entering validation, while the 1 MB cap protects node memory and network bandwidth.

`LedgerIndex` is typedefed as `std::uint32_t`, giving the ledger sequence a clear, named type rather than a raw integer — making function signatures self-documenting wherever ledger positions are passed. `TxID` aliases `uint256`, emphasising that a transaction identifier is a 256-bit hash, not an opaque blob.

`FLAG_LEDGER_INTERVAL = 256` drives the two helper functions `isFlagLedger()` and `isVotingLedger()`, both implemented identically in `Protocol.cpp` as `seq % FLAG_LEDGER_INTERVAL == 0`. Flag ledgers are the points at which network-wide fee and reserve voting takes effect; the fact that both functions share the same implementation body documents that voting happens on flag ledger boundaries.

## Ledger-Object Structural Limits

Several constants constrain the internal structure of ledger objects:

- `dirNodeMaxEntries = 32` — each page of an owner directory or offer directory holds at most 32 entries. This keeps page-traversal O(32) per node hop.
- `dirNodeMaxPages = 262144` — a historical cap on total directory pages, superseded by the `fixDirectoryLimit` amendment. It remains in the header as a documented artifact, illustrating that some constants are vestigial once network amendments retire them.
- `dirMaxTokensPerPage = 32` — mirrors `dirNodeMaxEntries` for NFT pages.
- `oversizeMetaDataCap = 5200` — the maximum number of metadata entries a single transaction may produce. Transactions touching too many objects are rejected before their metadata can grow unbounded.
- `maxDeletableDirEntries = 1000` — limits how many owner-directory entries an account may have before it becomes un-deletable. This prevents account deletion from consuming excessive compute in a single transaction.

## Offer and NFT Cleanup Limits

`unfundedOfferRemoveLimit = 1000` and `expiredOfferRemoveLimit = 256` cap the number of stale offers that may be cleaned up opportunistically during a single transaction pass. These limits are an explicit **performance tradeoff**: cleaning up more offers per pass reduces ledger bloat, but processing too many in a single transaction makes that transaction expensive and unpredictable in execution time. The asymmetry between 1000 and 256 reflects that unfunded-offer removal was designed to handle larger batches; the lower cap for expired offers reflects their different discovery path.

`maxTokenOfferCancelCount = 500` and `maxDeletableTokenOfferEntries = 500` apply the same principle to NFT offer cancellation and NFT burning, respectively — burning an NFT requires cleaning up all of its associated offers first, so an NFT with more than 500 live offers cannot be burned.

## Basis Points Arithmetic System

The header establishes a small but important type-safe arithmetic system around basis points (bips). Rather than manipulating raw integers that could silently represent percentages, drops, or fee-levels, the code uses `Bips<T>` and `TenthBips<T>` wrappers from `Units.h`.

Two compile-time constants anchor the system:
```cpp
Bips32 constexpr bipsPerUnity(100 * 100);          // 10,000 bps = 100%
TenthBips32 constexpr tenthBipsPerUnity(100'000);   // 100,000 tenth-bps = 100%
```
Both are validated by `static_assert`, making any accidental mis-initialisation a compile error rather than a runtime bug.

Four `constexpr` helper functions build on these anchors:
- `percentageToBips(p)` and `percentageToTenthBips(p)` convert an integer percentage into the typed unit.
- `bipsOfValue(value, bips)` and `tenthBipsOfValue(value, bips)` compute a proportional share of a `value` in the appropriate unit. Both are plain integer-division templates, making them usable with any numeric type `T`.

This design was chosen over floating-point arithmetic deliberately: all ledger calculations involving fees, rates, and percentages must be deterministic and reproducible on every validator. Integer basis-point arithmetic guarantees bit-identical results across platforms.

## Lending Sub-namespace

The `Lending` namespace groups rate limits specific to the on-ledger lending protocol. Every rate — management fee, coverage, interest, late interest, close interest, overpayment interest — is capped at 100% (expressed as `percentageToTenthBips(100)` = `TenthBips32(100'000)`), except `maxManagementFeeRate`, which is capped at 10% and stored as a `TenthBips16` (narrower type, since 10,000 fits in `uint16_t`).

Two transaction-execution constants control `LoanPay` batching behaviour:
- `loanPaymentsPerFeeIncrement = 5` — one base fee unit is charged per five estimated payments. This is documented as "chosen arbitrarily" and amendment-locked once released.
- `loanMaximumPaymentsPerTransaction = 100` — a hard cap on how many payments a single `LoanPay` will actually process, independent of the fee estimate. The comment explicitly warns that the cap and the fee estimate are decoupled, so a poorly constructed transaction could be charged for more payments than it actually processes. Requiring `loanMaximumPaymentsPerTransaction` to be a multiple of `loanPaymentsPerFeeIncrement` is a documented invariant, not enforced at compile time.

## Miscellaneous Field-Length and Feature Limits

The remaining constants cap the byte length of variable-length fields for newer ledger object types: NFT URIs, DID documents and URIs, domain fields, Oracle provider strings and symbol classes, credential types, and MPToken metadata. All are set to 256 bytes except `maxCredentialTypeLength` (64 bytes) and `maxMPTokenMetadataLength` (1024 bytes). The `maxMPTokenAmount` constant (`0x7FFF'FFFF'FFFF'FFFF`) is immediately validated against `Number::maxRep` via `static_assert`, ensuring that the XRPL numeric type can represent every valid MPToken quantity.

Vault-related constants (`vaultStrategyFirstComeFirstServe`, `vaultDefaultIOUScale`, `vaultMaximumIOUScale`, `maxAssetCheckDepth`) govern the Vault feature's IOU-to-share conversion scale (6–18) and recursive depth limit for nested vault assets (5 levels).

`maxBatchTxCount = 8` caps how many transactions can be submitted as a single atomic batch, directly bounding the worst-case compute cost for batch validation.