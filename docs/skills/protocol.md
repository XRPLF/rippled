# Protocol and Serialization

The protocol layer defines XRPL's wire format, type system, and validation rules. It owns the canonical binary encoding required for signatures and consensus, the macro-driven registries for features/transactions/ledger entries/sfields/permissions, the typed object model (`STBase` hierarchy) that every transaction and ledger object inhabits, the cryptographic primitives, and the JSON/RPC boundary.

## Layered Type System

```
Asset = std::variant<Issue, MPTIssue>     ← unified asset identity (XRP/IOU/MPT)
  Issue       = (Currency, AccountID)     ← XRP iff currency==zero
  MPTIssue    = wraps MPTID (192-bit: seq32 || account160)

Amount types (lean, runtime polymorphic via Asset):
  XRPAmount   = int64 drops               ← integral, no asset
  IOUAmount   = (mantissa, exponent)      ← 15-digit decimal floating point
  MPTAmount   = int64                     ← integral, no asset

STAmount = unified wire/serialized form holding Asset + value
  - holds<Issue>(), holds<MPTIssue>(), native(), integral()
  - canonicalize() normalizes (mantissa, exponent) per asset rules
  - Internal: mAsset + mValue(uint64) + mOffset(int) + mIsNegative(bool)
```

`PathAsset` = `std::variant<Currency, MPTID>` — pathfinding-only asset reference (no issuer); used inside `STPathElement`.

Conversion utilities in `AmountConversions.h`: `toSTAmount`, `toAmount<T>`, `getAsset<T>`. Lean→STAmount is implicit-friendly; STAmount→lean is explicit (`get<>` throws on type mismatch).

`Units.h` provides phantom-typed `ValueUnit<TAG, T>` (`Drops`, `FeeLevel`, `FeeLevelDouble`) with `unit_cast<>` for explicit conversion; prevents drop/fee-level mixups at compile time.

## Key Invariants

- **Canonical field ordering:** sort by `(SerializedTypeID << 16) | fieldValue`, NOT by raw Field ID bytes — wrong sort breaks signatures
- **Field ID encoding:** 1–3 bytes; both type and field codes <16 → single byte `(type<<4)|name`
- **Hash domain separation:** every signable payload prepends a 4-byte `HashPrefix` (`STX\0`, `SMT\0`, `VAL\0`, `BCH\0`, `CLM\0`, `LWR\0`, `MAN`, `TXN`, etc.) — never share hashes across domains. Helper `make_hash_prefix(a,b,c)` is constexpr `uint32_t` builder.
- **STObject access semantics:** `obj[sfFoo]` throws `FieldErr` if absent; `obj[~sfFoo]` returns `std::optional`. `getOrThrow<T>(name)` family in `json_get_or_throw.h` enforces presence + type for raw JSON inputs.
- **Amendment IDs are deterministic:** `featureFoo == sha512Half(Slice("Foo"))` — never change a feature name. Feature names must satisfy `isFeatureName()` at compile time (`UpperCamel` regex). Names exactly 32 bytes long are forbidden (reserved for raw hash collision prevention).
- **`numFeatures` is a ceiling, NOT an exact count.** Counting includes `XRPL_RETIRE_*` and any inactive macros; never use it as a length.
- **Feature registry frozen at startup:** `registerFeature` checks `numFeatures` and aborts via static-init `LogicError` if exceeded. The `readOnly` atomic fence flips after all file-scope variables are initialized — any query before then asserts.
- **Singletons everywhere:** `SField`, `LedgerFormats`, `TxFormats`, `InnerObjectFormats`, `Permission`, `Feature` registry all use Meyer's singletons; registration completes before `main()` via static init.
- **Multi-sign signers MUST be sorted ascending by AccountID** (no duplicates, count in [1,32], cannot include tx account). The signer's AccountID is appended to the multi-sign blob to prevent shared-RegularKey replay attacks.
- **`vfFullyCanonicalSig` always set** by signer; verifiers normalize ECDSA S to low form via libsecp256k1.
- **Amendment-gated arithmetic:** `getSTNumberSwitchover()` is a `LocalValue<bool>` (per-coroutine) selecting legacy vs `Number`-based normalization in `IOUAmount`/`STAmount`.
- **TxMeta `AffectedNodes` must be sorted by index** for canonical serialization (consensus-critical). `addRaw()` performs this sort; failure is a consensus fork risk.
- **STObject debug-only field-uniqueness checks** (`isFieldAllowed`): silent duplicate fields in production are possible bugs but no runtime check.
- **STLedgerEntry construction fails loudly** if the type is unrecognized — no silent fallback.
- **STValidation only accepts secp256k1 keys**; Ed25519 keys throw at construction time.
- **STNumber two-phase rounding contract:** `associateAsset()` must be called before `add()`. The assertion in `add()` checks idempotency — calling `setValue()` after `associateAsset()` without re-associating is a programming error.

## Macro-Driven Registries (X-Macros)

Single source of truth for each registry; `.macro` files included multiple times with redefined macros to generate enum, declarations, and definitions.

| Macro file | Used for | Add requires |
|---|---|---|
| `features.macro` | `XRPL_FEATURE`, `XRPL_FIX`, `XRPL_RETIRE_*` | Bump `numFeatures` in `Feature.h` |
| `transactions.macro` | `TRANSACTION(tag, value, name, delegable, amendment, privileges, fields)` | nothing — count derived |
| `ledger_entries.macro` | `LEDGER_ENTRY(tag, value, name, rpcName, fields)` + `LEDGER_ENTRY_DUPLICATE` for name collisions | nothing |
| `sfields.macro` | `TYPED_SFIELD(name, TYPE, code)`, `UNTYPED_SFIELD` | nothing |
| `permissions.macro` | `PERMISSION(name, txType, value)` (granular permissions ≥65537) | nothing |

Pattern uses `#pragma push_macro/pop_macro` to protect macro names. `UNWRAP(...)` strips outer parens around field-list initializers so commas don't confuse the preprocessor. `LEDGER_ENTRY_DUPLICATE` exists because `DepositPreauth` is both a transaction type and ledger entry type — `JSS()` can't emit the same string twice.

Feature name validation is `constexpr` (compile-time `static_assert` on the literal); typos like lower-case first letter fail to build.

The `FeatureCollections` internal singleton uses `boost::multi_index_container` with three simultaneous indexes: random-access by insertion order (`byIndex` for bitset mapping), hash-unique by `uint256`, and hash-unique by name. A simple `unordered_map` cannot provide the stable integer index that `FeatureBitset` requires.

## Field Identity (`SField`)

- **Field code** = `(SerializedTypeID << 16) | fieldValue` — packs type family and per-type index; canonical sort key
- `SField` instances are immutable singletons created at static init via `private_access_tag_t` (only definable inside `SField.cpp`)
- `TypedField<T>` adds compile-time payload type; `OptionaledField<T>` via `operator~(sfField)`
- Metadata flags (`fieldMeta`): `sMD_ChangeOrig`, `sMD_ChangeNew`, `sMD_DeleteFinal`, `sMD_Create`, `sMD_Always`, `sMD_BaseTen` (decimal display), `sMD_PseudoAccount`, `sMD_NeedsAsset` (drives `STTakesAsset` association), `sMD_Default` (field absent when zero)
- `IsSigning::no` excludes fields from signing hash (`sfTxnSignature`, `sfSigners`, `sfSignature`, etc.)
- `isBinary()` ⇔ `fieldValue<256` (wire-representable); `isDiscardable()` ⇔ `fieldValue>256` (JSON-only, e.g., `sfHash`, `sfIndex`)
- **Debug-only uniqueness check** during static init; release builds will silently mis-register on collision

## Wire Format Reference

| Item | Encoding |
|---|---|
| XRP STAmount | 8 bytes; bit63=0, bit62=sign(1=pos), 62-bit value |
| MPT STAmount | 8 bytes header (bit63=0, bit61=1, 56-bit value) + 192-bit MPTID |
| IOU STAmount | bit63=1, bit62=sign, 8-bit (offset+97), 54-bit mantissa, +20B currency, +20B issuer |
| AccountID | 20 bytes, VL-prefixed when standalone (`STAccount` mimics `STBlob` wire format) |
| MPTID | 192 bits = 32-bit big-endian sequence ‖ 160-bit issuer |
| STArray | elements between markers; ends with `STI_ARRAY,1` (`0xf1`) |
| STObject | fields in canonical order; ends with `STI_OBJECT,1` (`0xe1`) |
| VL prefix | 1 byte (0–192), 2 bytes (193–12480), 3 bytes (12481–918744); else `std::overflow_error` |
| STIssue | 160-bit currency; if zero → XRP; if next 160 = `noAccount()` → MPT (then 32-bit seq); else IOU issuer |
| STNumber | 12 bytes: int64 signed mantissa + int32 exponent (two separate statements — order must be explicit) |
| LP token currency | byte0 = `0x03`; bytes 1-19 = `sha512Half(min(asset1,asset2), max(asset1,asset2))` low bits |
| Order book quality | `(exponent+100) << 56 \| mantissa`; embedded in last 8 bytes of directory key (big-endian) so SHAMap order = price order |
| NFTokenID (256-bit) | flags(2) + transferFee(2) + issuer(20) + cipheredTaxon(4) + serial(4); low 96 bits = page sort key |
| LedgerHeader | 118 bytes fixed layout (seq, drops, parentHash, txHash, accountHash, parentClose/closeTime/closeFlags, closeTimeResolution) |
| Payment channel claim | `HashPrefix::paymentChannelClaim` ‖ channelID(32) ‖ amount(8) |
| Batch signing payload | `HashPrefix::batch` ‖ outer flags(4) ‖ inner-tx count(4) ‖ inner-tx hash list |

## Canonical Hashes

```
TXN  → transactionID    SND → txNode (with metadata)
MLN  → leafNode         MIN → innerNode
LWR  → ledgerMaster     STX → txSign (single-sig)
SMT  → txMultiSign      VAL → validation
PRP  → proposal         MAN → manifest
CLM  → paymentChannelClaim    BCH → batch
```

All hashes use `sha512Half` (first 256 bits of SHA-512). `HashPrefix` constants are protocol-immutable; the `make_hash_prefix(a,b,c)` constexpr packer in `HashPrefix.h` is the canonical way to declare new prefixes.

## STObject and STVar

- `STObject` stores `std::vector<detail::STVar>`; iterators expose `STBase const&` via transform iterator
- `STVar` is type-erased with 72-byte inline buffer (small-object optimization); `on_heap()` reports whether a value spilled; larger ST types heap-allocate
- `STVar` is movable; moving an on-heap STVar steals the pointer, while inline ones must invoke each ST type's move ctor through the v-table
- `copy()`/`move()` virtuals on every ST type delegate to `STBase::emplace()` for placement-new into `STVar`'s buffer
- **Two modes:**
  - **Free** (`mType==nullptr`): linear field scan via `getFieldIndex()`; accepts any field; insertion order preserved
  - **Templated** (`mType` set): O(1) field lookup via `SOTemplate::indices_`; `v_` laid out in template order with every slot pre-populated; unknown fields rejected
- `applyTemplate()` validates after deserialization; `set(SOTemplate)` initializes empty object with template
- Deserialization depth capped at 10 to prevent stack exhaustion
- `operator==` compares only `isBinary()==true` fields (O(n²) by design); `isEquivalent()` fast-paths when same `mType` pointer (positional comparison)
- `makeInnerObject()` applies templates conditionally on `fixInnerObjTemplate` / `fixInnerObjTemplate2` amendments — historical ledger entries without template structure must not be rejected on replay

### Proxy Access Pattern

```cpp
auto amt = tx[sfAmount];           // ValueProxy: throws FieldErr if absent
auto dst = tx[~sfDestination];      // OptionalProxy: std::optional
tx[sfFlags] = 0;                   // proxy.assign() — soeDEFAULT zero is silently removed
tx[~sfDestTag] = std::nullopt;     // remove field (only valid for soeOPTIONAL)
```

Proxies forbid removing `soeREQUIRED` or `soeDEFAULT` fields.

## SOEStyle (Field Presence)

| Style | Meaning |
|---|---|
| `soeREQUIRED` | must be present |
| `soeOPTIONAL` | may be absent; if present, may carry default value |
| `soeDEFAULT` | may be absent; if present, must NOT equal default — auto-removed when assigned default |

`SOETxMPTIssue` flag on amount/issue fields: `soeMPTSupported`, `soeMPTNotSupported`, `soeMPTNone`. Omitting `soeMPTSupported` silently rejects MPT amounts in that field.

## Common Bug Patterns

- Adding to `transactions.macro` without adding to `sfields.macro` → silent serialization failures
- Forgetting to bump `numFeatures` after `XRPL_FEATURE` → static-init `LogicError` (registry overflow) caught at startup
- Hand-built binary blobs in non-canonical field order → signature verification failures
- Omitting `soeMPTSupported` on amount field → MPT payments silently rejected
- Mutating `sfTransactionType` inside `STTx` assembler callback → `LogicError` (caught at startup)
- Storing `STBase` subclasses directly in `std::vector` → field names lost on copy-assignment slide; use `STArray`/`STObject` instead
- Storing `Currency` as `"XRP"` ISO code (`badCurrency()`) instead of zero → silently rejected; `to_currency()` legacy returns `badCurrency()` rather than failing
- Forgetting to call `associateAsset(sle, asset)` near end of `doApply()` for vault/loan transactors → unrounded `STNumber` values
- Returning `tec*` from `preflight()` → `NotTEC` type prevents this at compile time (would allow fee theft on unsigned tx)
- `TxMeta::AffectedNodes` left unsorted before serialization → consensus-fork risk
- Comparing `Issue` instances when one side is MPT-wrapped → `Issue::operator==` only compares currency+account; use `Asset` equality
- Relying on debug-only `assert` inside `STObject::isFieldAllowed` to catch duplicate fields in release
- Treating `numFeatures` as a length / iteration bound (it includes retired slots)
- Calling `setValue()` on an `STNumber` field after `associateAsset()` without re-associating → idempotency assertion fires in `add()`
- Using Ed25519 key with `STValidation` → throws at construction time (only secp256k1 allowed)
- Batch inner transaction `sfRawTransactions` array exceeds `maxBatchTxCount` (8) or contains nested `ttBATCH` → rejected by `passesLocalChecks()`
- `getNFTokenIDFromPage()` without the page-split guard: a `sfModifiedNode` for a third page may lack `sfNFTokens` in `sfPreviousFields`; skip silently
- `STNumber` JSON string parsing asserting `!getCurrentTransactionRules()` — string-format numbers not allowed inside active transaction processing

## Key Patterns

### Amendment Registration

```cpp
// In features.macro:
XRPL_FEATURE(MyFeature,  Supported::yes, VoteBehavior::DefaultNo)
XRPL_FIX    (MyBugFix,   Supported::yes, VoteBehavior::DefaultNo)
XRPL_RETIRE_FEATURE(OldFeature)  // code removed; remains registered for ledger compat
```

Lifecycle: `Supported::no/DefaultNo` → `Supported::yes/DefaultNo` → (rare) `DefaultYes` for critical fixes. **Never** revert `Supported::yes` to `no` (would amendment-block existing nodes).

`setCurrentTransactionRules()` has a non-obvious side effect: it calls `Number::setMantissaScale()` to push the precision mode (`small` vs `large`) without requiring `Number` to query rules on every arithmetic call.

### NotTEC vs TER

```cpp
NotTEC preflight(...);   // can only return tel/tem/tef/ter/tes (no tec)
TER    doApply(...);     // can return any code including tec*
```

`TERSubset<Trait>` enforces this at compile time via `enable_if`. `TERtoInt(v)` is the authorized free-function conversion (member `explicit operator` would be too permissive in initializer contexts).

### Signing / Verifying

```cpp
sign(st, HashPrefix::txSign, KeyType::secp256k1, sk);   // writes sfSignature
verify(st, HashPrefix::txSign, pubKey);                  // returns bool

// Multi-sign optimization (shared body, per-signer suffix):
auto s = startMultiSigningData(obj);
finishMultiSigningData(signerAccountID, s);  // append signer ID to shared payload
```

`addWithoutSigningFields()` excludes signature fields from the signed payload. Both `sign()` and `verify()` share the same serialization path (serialize once, check/set the field), ensuring they cannot diverge.

### Batch Signing

`serializeBatch()` (in `Batch.h`, inline) produces: `HashPrefix::batch ‖ outer flags(4) ‖ inner-tx count(4) ‖ inner-tx hash list`. Both `checkBatchSingleSign()` and `checkBatchMultiSign()` call this once; multi-sign appends the per-signer AccountID suffix via `finishMultiSigningData`. `passesLocalChecks()` rejects nested batches.

### STNumber + STTakesAsset

```cpp
// Vault/Loan/LoanBroker fields use STNumber (no asset embedded).
// In doApply(), after all mutations:
associateAsset(*sle, vaultAsset);  // rounds all sMD_NeedsAsset fields, removes zero-defaults
```

`STNumber` serializes a `Number` (signed mantissa+exponent, 12 bytes); rounding is asset-dependent and resolved by `associateAsset` walking fields flagged `sMD_NeedsAsset`. Fields with `sMD_Default` are removed from the SLE after rounding if the value became zero. `associateAsset()` is offset-based (the only path that yields mutable `STBase&`).

### LP Token Currency Derivation

```cpp
Currency lpc = ammLPTCurrency(asset1, asset2);  // canonical std::minmax
// byte 0 = 0x03 (LP marker), bytes 1..19 = low 152 bits of sha512Half(min, max)
```

### Pseudo-Account Synthesis

Pseudo-accounts (AMM, Vault, LoanBroker) carry a 256-bit synthesized ID in fields flagged `sMD_PseudoAccount` (`sfAMMID`, `sfVaultID`, `sfLoanBrokerID`). These identify a stateless account address derived from the owner ledger entry.

### NFT Token ID Recovery from Metadata

`getNFTokenIDFromPage()` uses set-difference: collect all token IDs from `sfPreviousFields` and `sfFinalFields` across all metadata nodes; assert `finalIDs.size() == prevIDs.size() + 1`; use `std::mismatch` to find the inserted entry. Guard: when a mint causes a page split, the third page's `sfModifiedNode` may have `sfPreviousFields` without `sfNFTokens` — check presence before extracting.

## STAmount Arithmetic Details

- **IOU canonical range:** mantissa ∈ [10^15, 10^16), exponent ∈ [-96, +80]; zero = (mantissa=0, exponent=-100)
- **Two rounding modes:** `mulRound`/`divRound` (legacy, rounds up when fractional ≥ 0.1) vs `mulRoundStrict`/`divRoundStrict` (correct remainder tracking, propagates `NumberRoundModeGuard`)
- **Overflow guard in multiply:** if `min(a,b) > sqrt(cMaxNative)`, product overflows — checked before 128-bit intermediate
- **`canAdd`/`canSubtract`:** for IOU, uses round-trip relative error test with 10^-4 tolerance
- **`areComparable()`:** uses `std::visit` over `Asset` variant; incompatible asset types throw immediately
- Feature-gated: `featureSingleAssetVault` / `featureLendingProtocol` gate the `fromNumber()` path in `operator=(Number const&)`

## QualityFunction (AMM Path Optimization)

`q(out) = m * out + b` where `b` = reciprocal-rate intercept, `m` = AMM price-impact slope.

- **`AMMTag`:** `m_ = -fee/poolIn`, `b_ = poolOut*fee/poolIn` — derived from single-path AMM swap formula
- **`CLOBLikeTag`:** `m_ = 0`, `b_ = 1/quality.rate()` — also used for multi-path AMM (fixed allocation = constant quality)
- **`combine()`:** `m_ += b_ * qf.m_; b_ *= qf.b_` — linear function composition; clears `quality_` cache when slope becomes nonzero
- **`outFromAvgQ()`:** solves `out = (1/rate - b_) / m_`; rounding mode `upward` to conservatively bound output; returns `nullopt` if `m_==0`, rate==0, or `out<=0`
- `saveNumberRoundMode` RAII guard scopes the upward-rounding to just this computation

## AccountID Cache

Direct-mapped cache in `AccountID.cpp`. Indexed by `hardened_hash<>` (xxHash + random seed = DoS-resistant). Lock sharding: single `atomic<uint64_t> locks_` encodes 64 independent spinlocks via `packed_spinlock` (one per `index % 64`). Edge case: `encoding[0] != 0` guard distinguishes an uninitialized slot from a legitimate cache hit for the all-zero `xrpAccount()`. Cache is optional; `initAccountIdCache(0)` disables it entirely.

## Cross-Chain Bridge Attestations

Two parallel hierarchies: `Attestations::AttestationClaim` / `AttestationCreateAccount` (full, with signature — what witnesses submit) vs `XChainClaimAttestation` / `XChainCreateAccountAttestation` (ledger-stored, signature stripped). Conversion constructors project signing→storage in one step.

`AttestationMatch` three-state enum: `match`, `matchExceptDst`, `nonDstMismatch`. `XChainAddClaimAttestation` requires `match`; `XChainClaim` (user-specified dst) accepts `matchExceptDst`. `sameEvent()` ignores signer identity fields; full `operator==` requires all fields.

Max attestations per container: 256 (far above any real witness set; guards memory allocation).

## Critical Files

### Foundations
- `include/xrpl/protocol/SField.h`, `src/libxrpl/protocol/SField.cpp` — field registry, X-macro expansion, code packing
- `include/xrpl/protocol/Feature.h`, `src/libxrpl/protocol/Feature.cpp` — `numFeatures` (ceiling!), `FeatureBitset`, `registerFeature` with compile-time name validation; `readOnly` atomic fence
- `include/xrpl/protocol/Rules.h`, `src/libxrpl/protocol/Rules.cpp` — `Rules` snapshot of enabled amendments; `CurrentTransactionRules` is a `LocalValue<Rules const*>` (per-coroutine); `isFeatureEnabled()` queries thread-local; `setCurrentTransactionRules` pushes `Number` mantissa scale
- `include/xrpl/protocol/HashPrefix.h` — protocol-immutable domain separators; `make_hash_prefix` constexpr packer

### Macro Tables (single sources of truth)
- `include/xrpl/protocol/detail/features.macro`
- `include/xrpl/protocol/detail/transactions.macro`
- `include/xrpl/protocol/detail/ledger_entries.macro`
- `include/xrpl/protocol/detail/sfields.macro`
- `include/xrpl/protocol/detail/permissions.macro`

### Type System Roots
- `STBase.h/cpp` — polymorphic root; `emplace()` SOO helper; `JsonOptions`; `STExchange` traits glue
- `STObject.h/cpp` — heterogeneous container, proxy system, template enforcement, debug uniqueness asserts
- `STVar` (`detail/STVar.h`) — 72-byte inline variant; `on_heap()`; move steals pointer when heap-allocated; depth guard at 10
- `SOTemplate.h/cpp` — schema with O(1) field index; move-only; carries `SOEStyle` + `SOETxMPTIssue`

### Format Registries
- `TxFormats.h/cpp`, `LedgerFormats.h/cpp`, `InnerObjectFormats.h/cpp` — all inherit `KnownFormats<Key, Derived>` with `forward_list<Item>` (pointer-stable) + dual flat_maps
- `LedgerEntry` rpcName vs name distinction enables `DepositPreauth` collision handling

### Amount / Asset Stack
- `Asset.h/cpp` — variant of Issue/MPTIssue; `visit()`, `equalTokens()`, `BadAsset` sentinel
- `Issue.h/cpp`, `MPTIssue.h/cpp` — XRP/IOU and MPT identity; **note** `Issue::operator==` ignores MPT-ness — always go through `Asset`
- `STAmount.h/cpp` — unified serialized amount; `canMul`/`canAdd`/`canSubtract` safety checks; `mulRound`/`mulRoundStrict` (legacy vs precise rounding); `roundToScale`
- `STNumber.h/cpp` — `Number`-typed field; pairs with `STTakesAsset` infrastructure; 12-byte wire: int64 mantissa + int32 exponent
- `STIssue.h/cpp`, `STCurrency.h/cpp` — asset-only fields
- `STTakesAsset.h/cpp` — `associateAsset` walks `sMD_NeedsAsset` fields, rounds + strips zero-defaults; include order: `STTakesAsset.h` before `STLedgerEntry.h`
- `IOUAmount.h/cpp`, `XRPAmount.h`, `MPTAmount.h/cpp` — lean representations
- `Rate2.h` — `Rate` newtype with `parityRate = 1_000_000_000`; transfer-rate math
- `Units.h` — phantom-typed `Drops`/`FeeLevel`
- `Number` (in `xrpl/basics/`) — high-precision arithmetic; `MantissaRange::large` enabled by SingleAssetVault/LendingProtocol amendments
- `AmountConversions.h` — typed coercions

### Cryptography
- `PublicKey.h/cpp` — 33-byte unified format (0xED prefix for Ed25519); `ECDSACanonicality` enum (canonical vs fullyCanonical); libsecp256k1 normalization
- `SecretKey.h/cpp` — `secure_erase` in dtor; deleted `==`/`<<`; XRPL-specific secp256k1 derivation via `Generator`
- `Seed.h/cpp` — 128-bit; `parseGenericSeed()` cascades hex→base58→RFC1751→passphrase, rejecting other key types first
- `detail/secp256k1.h` — libsecp256k1 context singleton via template-with-default-param trick (ODR-safe header-only); created with `SIGN|VERIFY` flags combined
- `digest.h` — `sha512Half`, `sha512_half_hasher_s` (secure erase variant)
- `tokens.h/cpp` (+ `b58_utils.h`, `token_errors.h`) — Base58Check; fast path uses base 58^10 intermediate (10–15× speedup via `unsigned __int128`, gated on non-MSVC); `TokenType` enum is protocol-stable; `alphabetReverse` is `constexpr` 256-element array; leading-zero bytes each map to `'r'`

### Wire I/O
- `Serializer.h/cpp` — accumulator; `addVL`, `addFieldID`, big-endian integers, `getSHA512Half()`
- `SerialIter` — non-owning forward cursor over a byte buffer; throws on underrun
- `Sign.h/cpp` — `sign`/`verify` with HashPrefix prepended to `addWithoutSigningFields()` output; `startMultiSigningData`/`finishMultiSigningData` split for batch-signer optimization; `signingForID` helper for arbitrary payload bytes
- `Batch.h` — inline `serializeBatch()`: `HashPrefix::batch ‖ flags(4) ‖ count(4) ‖ txids`
- `serialize.h` — top-level convenience helpers
- `messages.h` — protobuf message tag constants (`TYPE_BOOL` undef guard documented)

### Higher-Level Objects
- `STTx.h/cpp` — caches `tid_` and `tx_type_`; `passesLocalChecks` (memos, pseudo-tx, MPT support, batch nesting, max 8 inner txs); `sterilize()` round-trip; `getBatchTransactionIDs()` lazy + immutable after first call; `getFeePayer()` returns `sfDelegate` or `sfAccount`; `checkSign()` dispatches single/multi/batch/counterparty; SQL helpers (`getMetaSQL`)
- `STLedgerEntry.h/cpp` (alias `SLE`) — typed ledger object; `thread()` updates `sfPreviousTxnID`; `isThreadedType()` gated by `fixPreviousTxnID`; `getJson()` injects `jss::index` and synthetic `mpt_issuance_id` for MPT issuances
- `STValidation.h/cpp` — lazy `valid_` cache; `mTrusted` separate from validity; `lookupNodeID` callback decouples manifest system; `validationFormat()` is function-local static (SField init order safety); `sfCookie` is `soeDEFAULT` to prevent fingerprinting
- `STArray.h/cpp`, `STVector256.h/cpp`, `STBitString.h/cpp`, `STInteger.h/cpp`, `STBlob.h/cpp`, `STAccount.h/cpp`, `STPathSet.h/cpp`, `STXChainBridge.h/cpp`

### Transaction Meta
- `TxMeta.h/cpp` — `AffectedNodes` (sorted by index in `addRaw()`!), `DeliveredAmount`, `sfParentBatchID`; linear scan for node lookup (bounded by 32-slot reservation); `getAffectedAccounts()` must match JS `Meta#getAffectedAccounts`
- `LedgerHeader.h/cpp` — 118-byte fixed serialization; close-time-resolution fudging

### Indexes and Keys
- `Indexes.h/cpp` — `keylet::*` factories with `LedgerNameSpace` tagged hashing; `keylet::quality()` embeds 64-bit quality in last 8 bytes (big-endian); `keylet::amm()` uses `std::minmax` + `if constexpr` for heterogeneous token types; `nftpage` = owner(160 bits) ‖ masked token(96 bits) — range scan, no hash
- `Keylet.h/cpp` — type-tagged `(uint256, LedgerEntryType)`; `ltANY` wildcard, `ltCHILD` rejects directories
- `Protocol.h` — protocol-wide constants (`FLAG_LEDGER_INTERVAL`, etc.)
- `nftPageMask.h`, `nft.h` — NFT page boundary (low 96 bits); composite keys (high 160 = owner AccountID)
- `NFTokenID.h/cpp` — flags(2)+fee(2)+issuer(20)+cipheredTaxon(4)+serial(4); LCG `384160001 * seq + 2459` ciphers taxon; `getNFTokenIDFromPage()` and `getNFTokenIDFromDeletedOffer()` for metadata enrichment
- `NFTokenOfferID.h/cpp`, `NFTSyntheticSerializer.h/cpp` — derived/synthetic NFT entries; consumed by Clio as public API
- `Book.h` — `(in_asset, out_asset)` order-book identity
- `SeqProxy.h/cpp` — sequence vs ticket abstraction; sequence-type values sort before ticket-type values

### Validation Helpers (return NotTEC, preflight-time)
- `AMMCore.h/cpp` — `invalidAMMAsset`, `invalidAMMAssetPair`, `invalidAMMAmount`; `ammLPTCurrency()` uses canonical `std::minmax`
- `Permissions.h/cpp` — singleton; `isDelegable()` checks granular vs transaction-level (`<UINT16_MAX` boundary), amendment, delegable flag

### RPC / JSON Boundary
- `STParsedJSON.h/cpp` — depth cap 64; field-path-qualified errors via `make_name`; recognizes `"Payment"`, `"tesSUCCESS"`, etc.
- `ErrorCodes.h/cpp` — append-only enum; `sortedErrorInfos` validated at compile time; `warning_code_i` distinct from `error_code_i`
- `RPCErr.h/cpp` — `RPC::Status`/`make_error` helpers
- `ApiVersion.h` — `apiMinimumSupportedVersion`(1), `apiMaximumSupportedVersion`(2), `apiBetaVersion`(3); `apiVersionIfUnspecified`(1); `forAllApiVersions` / `forApiVersions` templates pass version as `integral_constant` for compile-time branching; `getAPIVersionNumber()` returns `apiInvalidVersion`(0) on parse failure; `setVersion()` has v1 legacy semver-string shim
- `MultiApiJson.h` — per-API-version `Json::Value` array indexed `[version - RPC::apiMinimumVersion]`; composes with `forAllApiVersions` from `ApiVersion.h`; preserves wire compatibility across versions
- `jss.h` — every JSON key as `Json::StaticString` via `JSS(name)` macro; PascalCase = protocol fields, snake_case = RPC
- `json_get_or_throw.h` — `getOrThrow<T>(jv, name)` specializations enforce presence + type; standard idiom for parsing untrusted JSON
- `st.h` — convenience aggregate header for all ST types

### Specialized Types
- `STIssue`, `STAccount` (160-bit, VL-encoded), `STBitString<Bits>`, `STInteger<T>`, `STBlob`, `STArray`, `STVector256`, `STCurrency`, `STPathSet`, `STXChainBridge`, `STNumber` (asset-contextual)
- `Quality.h/cpp` — inverted encoding (lower uint64 = higher quality); `ceil_in`/`ceil_out` proportional scaling; `_strict` variants honor Number rounding mode
- `QualityFunction.h/cpp` — linear `q(out)=m*out+b`; AMMTag (slope from pool) vs CLOBLikeTag (m=0); `combine()` for multi-step strands; `outFromAvgQ()` solves for capped output
- `XChainAttestations.h/cpp` — `Attestations::` namespace (full, with signature) vs `xrpl::` (stored); `match()` returns three-state `AttestationMatch`

### Pseudo-Account Fields (`sMD_PseudoAccount`)
- `sfAMMID`, `sfVaultID`, `sfLoanBrokerID` — 256-bit hash representing a synthesized account address

### Misc / System
- `BuildInfo.h/cpp` — version string, `getVersionString()` consumed by manifest/handshake
- `SystemParameters.h` — drops-per-XRP, `INITIAL_XRP`, ledger-related constants; validated by `static_assert`
- `UintTypes.h` — `uint256`/`uint160`/`uint128` aliases and tagged variants (`Currency`, `NodeID`, etc.)
- `TER.h/cpp` — error code enum families + `TERSubset`
- `TxFlags.h` — X-macro driven flag tables (`tf*`); see TxFlags Architecture below
- `TxFormats.h/cpp` — transaction-type → field schema
- `AccountID.h/cpp` — `calcAccountID()` = SHA-256 then RIPEMD-160 (matches Bitcoin for security argument); `AccountIdCache` direct-mapped with spinlock sharding

## Numeric Encoding Reference

```
IOU canonical:     mantissa ∈ [10^15, 10^16), exponent ∈ [-96, +80]
                   zero = (mantissa=0, exponent=-100) — sorts below smallest positive
XRP max:           cMaxNativeN = 10^17 drops (100 billion XRP)
MPT max:           maxMPTokenAmount = INT64_MAX = 0x7FFFFFFFFFFFFFFF
Transfer rate:     Rate{value} where value/1_000_000_000 = 1.0 (parityRate = 1:1)
NFT transfer fee:  uint16 basis points (0–50000), convert via nft::transferFeeAsRate (×10000)
AMM auction fee:   basis points; trading fee in tenths of basis points (10000 = 1%)
STNumber mantissa: int64 signed; when MantissaRange::large active, accessor divides by 10 to fit wire format
```

## Protocol-Stable Constants (NEVER CHANGE)

- `LedgerEntryType` numeric values (in ledger objects)
- `TxType` numeric values (in signed transactions)
- `SerializedTypeID` and `SField` codes (in serialized fields)
- `LedgerNameSpace` discriminator characters (in keylet derivation) — legacy `CONTRACT`, `GENERATOR`, `NICKNAME` reserved even though deprecated
- `HashPrefix` enum values (in signature/hash domain separation)
- `error_code_i` and `warning_code_i` numeric values (clients depend on them; append-only)
- `TECcodes` (and other `TER` family numeric values) — recorded in transaction metadata
- `TokenType` (Base58Check prefix bytes for accounts/seeds/nodes)
- LP token currency prefix byte (`0x03`)
- Universal transaction flags (`tfFullyCanonicalSig`, `tfInnerBatchTxn`)
- `FLAG_LEDGER_INTERVAL = 256` (drives consensus timing)
- `INITIAL_XRP = 100B × 10^6 drops` (validated by `static_assert` against `Number::maxRep`)
- NFT taxon LCG constants (`384160001 * seq + 2459`)
- All flag bit values (`tf*`, `lsf*`, `asf*`)
- XRPL Base58 alphabet (first char `'r'` for `AccountID=0` is cosmetically significant)
- `maxBatchTxCount = 8` (inner transactions per batch)

Changing any requires an amendment with explicit detection logic for old/new behavior.

## TxFlags Architecture

`TxFlags.h` is itself X-macro driven. Per-transaction flag groups are declared so that:

- Each group has `tf*` named bit constants
- `tfUniversalMask` is the union of universal flags (`tfFullyCanonicalSig`, `tfInnerBatchTxn`)
- Per-transaction `tf*Mask` constants are auto-computed via `MASK_ADJ` so that mask matches the declared flags exactly — adding a flag automatically updates the mask
- `TF_FLAG2` marks flags whose meaning was changed by an amendment; old/new bits coexist with disjoint enable conditions
- Inner-batch flag `tfInnerBatchTxn` is special: marks a tx as a member of a batch (skipped by ordinary preflight signature checks)

Pattern: when adding a new flag, define it in `TxFlags.h` in the appropriate group; do NOT manually adjust the mask — the macro derives it.

## STTx Construction Paths

Three constructors, all terminate with `tid_ = getHash(HashPrefix::transactionID)`:

1. **Wire** (`SerialIter&`) — hottest path; enforces `txMinSizeBytes` (32) and `txMaxSizeBytes` (1 MB) before field parsing; `set(sit)` returning `true` (inner object terminator found at top level) → throws
2. **Object promotion** (`STObject&&`) — no size check; `applyTemplate` enforces conformance
3. **Programmatic** (`TxType, assembler`) — installs template first; asserts `sfTransactionType` unchanged after assembler runs; `LogicError` (not `std::runtime_error`) on mutation

`getSeqProxy()` unifies `sfSequence` (classic) and `sfTicketSequence` (ticket); when `sfSequence==0` and `sfTicketSequence` present → ticket mode. Sequence-type always sorts before ticket-type.

`getFeePayer()` returns `sfDelegate` if present, else `sfAccount`. Authorization is enforced in `Transactor::checkPermission`, not here.

## Counterparty Signing (`sfCounterpartySignature`)

Used by `LoanSet` — allows a second party to sign the same transaction. `checkSign(Rules const&)` checks primary then counterparty (if field present). Errors from counterparty check are prefixed `"Counterparty: "`. `sign()` accepts optional `signatureTarget` reference to write into a sub-object.
