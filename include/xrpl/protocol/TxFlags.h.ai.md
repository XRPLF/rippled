# `TxFlags.h` — Transaction Flag Constants and Validation Masks

This header is the canonical, single-source-of-truth definition for every transaction flag in the XRPL protocol. It lives inside `include/xrpl/protocol/` alongside `LedgerFormats.h`, from which it borrows several `lsf*` and `lsmf*` ledger-state-flag values. Any code that validates a transaction's `Flags` field or that needs to enumerate flags for introspection (e.g., the `server_definitions` RPC) includes this header.

## Protocol Safety Warning

The file's own documentation delivers an unusually direct caution: flag values are part of the consensus protocol. Altering a constant without special amendment handling will cause a **hard fork** — transactions that were valid on old nodes become invalid on new ones, or vice versa. This explains several defensive patterns throughout the file, including the retention of the deprecated `tfTrustLine` constant and the careful layering of amendment-gated mask variants for NFToken minting.

## Flag Namespace Layout

All 32 bits of the `Flags` field (`FlagValue = std::uint32_t`) are partitioned by convention:

- **Bits 31–24 (high 8 bits):** Universal flags, shared across all transaction types. Currently two are defined: `tfFullyCanonicalSig` (bit 31, now always enforced by the network but retained for backward compatibility) and `tfInnerBatchTxn` (bit 30, marks an inner transaction inside a `Batch`). The OR of these is `tfUniversal`; its complement is `tfUniversalMask`.
- **Bits 23–0 (low 24 bits):** Transaction-specific flags. The same numeric bit can mean entirely different things in an `AccountSet` versus an `OfferCreate`.

## The X-Macro Engine

The dominant design pattern in this file is a single master X-macro, `XMACRO`, that lists every transaction type together with its flags. Rather than writing out constants, masks, and accessor functions three times and risking them drifting apart, the file instantiates `XMACRO` three times — once for each concern — using a different set of helper macros.

**Instantiation 1 — value declarations** (`TO_VALUE`/`NULL_NAME`/`NULL_OUTPUT`/`NULL_MASK_ADJ`): emits `inline constexpr FlagValue tfXxx = 0x...;` for every flag constant. The `TF_FLAG` macro introduces a new constant; `TF_FLAG2` suppresses the declaration, acting as a pure reference to a constant already declared by an earlier transaction (e.g., `tfLPToken` is declared in `AMMDeposit` and *referenced* in `AMMWithdraw` without redeclaration).

**Instantiation 2 — mask generation** (`TO_MASK`/`VALUE_TO_MASK`/`MASK_ADJ_TO_MASK`): emits one `tfXxxMask` constant per transaction. A mask is the bitwise NOT of all valid flags for that type unioned with the universal flags: `~(tfUniversal | flag1 | flag2 | ...)`. During validation, any transaction whose `Flags` field has a bit set in the mask is rejected — it contains either an unknown flag or one that is illegal for this transaction type.

**Instantiation 3 — per-type getter functions** (`TO_MAP`/`VALUE_TO_MAP`): emits `inline FlagMap const& getXxxFlags()` for each transaction, returning a `std::map<std::string, FlagValue>` that names each flag. These are collected by `getAllTxFlags()` into a `FlagMapPairList` (vector of `{txTypeName, FlagMap}` pairs). All getter functions use Meyer's singleton (`static FlagMap const flags = {...}`) to avoid the static initialization order fiasco while remaining zero-overhead on subsequent calls. `getAllTxFlags()` feeds the `server_definitions` RPC endpoint, which clients use to auto-discover the protocol's flag vocabulary at runtime.

The macro push/pop guard (`#pragma push_macro` / `#pragma pop_macro`) wrapping the entire block protects consuming translation units from having `XMACRO`, `TO_VALUE`, and similar short names leak into their scope, which would otherwise clobber any macros of the same name in headers included later.

## The `MASK_ADJ` Mechanism

Each `TRANSACTION(...)` invocation includes a `MASK_ADJ(value)` argument. For nearly all transactions this is `MASK_ADJ(0)` — a no-op. The exception is `Batch`, which specifies `MASK_ADJ(tfInnerBatchTxn)`. Because `tfInnerBatchTxn` is a universal flag (excluded from `tfUniversal`'s complement by default, meaning it would ordinarily be *allowed* on any transaction), `Batch` needs to actively reject it on the outer transaction wrapper. `MASK_ADJ` OR-ORs the specified bits *back into* the otherwise-clear positions of the generated mask, making them illegal for that specific transaction type.

Two `static_assert`s enforce this invariant at compile time: `tfBatchMask & tfInnerBatchTxn` must equal `tfInnerBatchTxn` (the outer Batch rejects the flag), while `tfPaymentMask & tfInnerBatchTxn` and `tfAccountSetMask & tfInnerBatchTxn` must equal zero (inner transactions may legitimately carry it). The `Batch` transactor itself checks at runtime that every inner transaction *does* have `tfInnerBatchTxn` set, completing the bidirectional enforcement.

## MPToken and Vault Flags — Ledger-State Mirroring

`MPTokenIssuanceCreate` flags are intentionally set to the same numeric values as the corresponding ledger-state flags (`lsfMPTCanLock`, `lsfMPTRequireAuth`, etc.) from `LedgerFormats.h`. This mirroring means the issuance transaction's `Flags` field is copied almost verbatim into the created `MPTokenIssuance` object's flags field, eliminating a translation step. The `tfMPTLocked` flag is deliberately *omitted* from the transaction — the comment notes it is not allowed to be set at creation time.

A second tier of MPToken-related constants uses the `tmf` prefix (transaction mutable flags): `tmfMPTCanMutate*` constants alias the `lsmf*` mutable-flag values from `LedgerFormats.h` and are used to decide which MPToken properties may be updated after issuance. The complementary `tmfMPTokenIssuanceSetMutableMask` and `tmfMPTokenIssuanceCreateMutableMask` follow the same inverted-mask validation pattern.

## Backward Compatibility Layers

The deprecated `tfTrustLine` (0x00000004, NFTokenMint) must remain defined for nodes processing historical ledger transactions minted before the `fixRemoveNFTokenAutoTrustLine` amendment. That amendment closed a reserve-exhaustion attack where two accounts could endlessly trade an NFToken, forcing unbounded trust lines onto the issuer. The file preserves three mask variants to accommodate the amendment timeline: `tfNFTokenMintMaskWithoutMutable` (base case), `tfNFTokenMintOldMask` (pre-amendment, allows `tfTrustLine`), and `tfNFTokenMintOldMaskWithMutable` (pre-amendment plus `featureDynamicNFT`).

## AccountSet Set/Clear Flags

A second, independent X-macro `ACCOUNTSET_FLAGS` defines the numeric *values* passed via the `SetFlag` and `ClearFlag` fields of `AccountSet` transactions. These are small integers (1–17) rather than bitmasks and are a distinct mechanism from the `Flags` bitmask system. They drive `getAsfFlagMap()`, another Meyer's singleton used by `server_definitions` to expose the `asf*` constants by name alongside the bitflag maps.

## Additional Composite Masks

Outside the X-macro, the file defines several convenience constants: `tfMPTPaymentMask` restricts payments involving MPTokens to only `tfPartialPayment`; `tfTrustSetPermissionMask` constrains the subset of `TrustSet` flags available when a transaction is used purely for permission operations; and `tfWithdrawSubTx` / `tfDepositSubTx` combine the mutually exclusive AMM mode flags into bitmasks used to validate that exactly one deposit or withdrawal mode is selected.