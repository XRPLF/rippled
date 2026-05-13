# Protocol and Serialization

The protocol layer defines XRPL's wire format, type system, and validation rules. It owns the canonical binary encoding required for signatures and consensus, the macro-driven registries for features/transactions/ledger entries, and the typed object model (`STBase` hierarchy) that every transaction and ledger object inhabits.

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
```

Conversion utilities in `AmountConversions.h`: `toSTAmount`, `toAmount<T>`, `getAsset<T>`. Lean→STAmount is implicit-friendly; STAmount→lean is explicit (`get<>` throws on type mismatch).

## Key Invariants

- **Canonical field ordering:** sort by `(SerializedTypeID << 16) | fieldValue`, NOT by raw Field ID bytes — wrong sort breaks signatures
- **Field ID encoding:** 1–3 bytes; both type and field codes <16 → single byte `(type<<4)|name`
- **Hash domain separation:** every signable payload prepends a 4-byte `HashPrefix` (`STX\0`, `SMT\0`, `VAL\0`, `STX\0`, `BCH\0`, `CLM\0`, `LWR\0`, etc.) — never share hashes across domains
- **STObject access semantics:** `obj[sfFoo]` throws `FieldErr` if absent; `obj[~sfFoo]` returns `std::optional`
- **Amendment IDs are deterministic:** `featureFoo == sha512Half(Slice("Foo"))` — never change a feature name
- **Singletons everywhere:** `SField`, `LedgerFormats`, `TxFormats`, `InnerObjectFormats`, `Permission`, `Feature` registry all use Meyer's singletons; registration completes before `main()` via static init
- **Multi-sign signers MUST be sorted ascending by AccountID** (no duplicates, count in [1,32], cannot include tx account)
- **`vfFullyCanonicalSig` always set** by signer; verifiers normalize ECDSA S to low form
- **Amendment-gated arithmetic:** `getSTNumberSwitchover()` is a `LocalValue<bool>` (per-coroutine) selecting legacy vs `Number`-based normalization in `IOUAmount`/`STAmount`

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

## Field Identity (`SField`)

- **Field code** = `(SerializedTypeID << 16) | fieldValue` — packs type family and per-type index; canonical sort key
- `SField` instances are immutable singletons created at static init via `private_access_tag_t` (only definable inside `SField.cpp`)
- `TypedField<T>` adds compile-time payload type; `OptionaledField<T>` via `operator~(sfField)`
- Metadata flags (`fieldMeta`): `sMD_ChangeOrig`, `sMD_ChangeNew`, `sMD_DeleteFinal`, `sMD_Create`, `sMD_Always`, `sMD_BaseTen` (decimal display), `sMD_PseudoAccount`, `sMD_NeedsAsset` (drives `STTakesAsset` association)
- `IsSigning::no` excludes fields from signing hash (`sfTxnSignature`, `sfSigners`, `sfSignature`, etc.)
- `isBinary()` ⇔ `fieldValue<256` (wire-representable); `isDiscardable()` ⇔ `fieldValue>256` (JSON-only, e.g., `sfHash`, `sfIndex`)

## Wire Format Reference

| Item | Encoding |
|---|---|
| XRP STAmount | 8 bytes; bit63=0, bit62=sign(1=pos), 62-bit value |
| MPT STAmount | 8 bytes header (bit63=0, bit61=1) + 192-bit MPTID |
| IOU STAmount | bit63=1, bit62=sign, 8-bit (offset+97), 54-bit mantissa, +20B currency, +20B issuer |
| AccountID | 20 bytes, VL-prefixed when standalone (`STAccount` mimics `STBlob` wire format) |
| STArray | elements between markers; ends with `STI_ARRAY,1` (`0xf1`) |
| STObject | fields in canonical order; ends with `STI_OBJECT,1` (`0xe1`) |
| VL prefix | 1 byte (0–192), 2 bytes (193–12480), 3 bytes (12481–918744); else `std::overflow_error` |
| STIssue | 160-bit currency; if zero → XRP; if next 160 = `noAccount()` → MPT (then 32-bit seq); else IOU issuer |
| LP token currency | byte0 = `0x03`; bytes 1-19 = `sha512Half(min(asset1,asset2), max(asset1,asset2))` low bits |
| Order book quality | `(exponent+100) << 56 \| mantissa`; embedded in last 8 bytes of directory key (big-endian) so SHAMap order = price order |
| NFTokenID (256-bit) | flags(2) + transferFee(2) + issuer(20) + cipheredTaxon(4) + serial(4); low 96 bits = page sort key |

## Canonical Hashes

```
TXN  → transactionID    SND → txNode (with metadata)
MLN  → leafNode         MIN → innerNode
LWR  → ledgerMaster     STX → txSign (single-sig)
SMT  → txMultiSign      VAL → validation
PRP  → proposal         MAN → manifest
CLM  → paymentChannelClaim    BCH → batch
```

All hashes use `sha512Half` (first 256 bits of SHA-512). `HashPrefix` constants are protocol-immutable.

## STObject and STVar

- `STObject` stores `std::vector<detail::STVar>`; iterators expose `STBase const&` via transform iterator
- `STVar` is type-erased with 72-byte inline buffer (small-object optimization); larger types heap-allocate
- `copy()`/`move()` virtuals on every ST type delegate to `STBase::emplace()` for placement-new into `STVar`'s buffer
- **Two modes:**
  - **Free** (`mType==nullptr`): linear field scan, accepts any field
  - **Templated** (`mType` set): O(1) field lookup via `SOTemplate::indices_`, template enforced
- `applyTemplate()` validates after deserialization; `set(SOTemplate)` initializes empty object with template
- Deserialization depth capped at 10 to prevent stack exhaustion

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
- Forgetting to bump `numFeatures` after `XRPL_FEATURE` → out-of-bounds access in `FeatureBitset`
- Hand-built binary blobs in non-canonical field order → signature verification failures
- Omitting `soeMPTSupported` on amount field → MPT payments silently rejected
- Mutating `sfTransactionType` inside `STTx` assembler callback → `LogicError` (caught at startup)
- Storing `STBase` subclasses directly in `std::vector` → field names lost on copy-assignment slide; use `STArray`/`STObject` instead
- Storing `Currency` as `"XRP"` ISO code (`badCurrency()`) instead of zero → silently rejected; `to_currency()` legacy returns `badCurrency()` rather than failing
- Forgetting to call `associateAsset(sle, asset)` near end of `doApply()` for vault/loan transactors → unrounded `STNumber` values
- Returning `tec*` from `preflight()` → `NotTEC` type prevents this at compile time (would allow fee theft on unsigned tx)

## Key Patterns

### Amendment Registration

```cpp
// In features.macro:
XRPL_FEATURE(MyFeature,  Supported::yes, VoteBehavior::DefaultNo)
XRPL_FIX    (MyBugFix,   Supported::yes, VoteBehavior::DefaultNo)
XRPL_RETIRE_FEATURE(OldFeature)  // code removed; remains registered for ledger compat
```

Lifecycle: `Supported::no/DefaultNo` → `Supported::yes/DefaultNo` → (rare) `DefaultYes` for critical fixes. **Never** revert `Supported::yes` to `no` (would amendment-block existing nodes).

### NotTEC vs TER

```cpp
NotTEC preflight(...);   // can only return tel/tem/tef/ter/tes (no tec)
TER    doApply(...);     // can return any code including tec*
```

`TERSubset<Trait>` enforces this at compile time via `enable_if`. `TERtoInt(v)` is the authorized free-function conversion (member `explicit operator` would be too permissive in initializer contexts).

### Signing/Verifying

```cpp
sign(st, HashPrefix::txSign, KeyType::secp256k1, sk);   // writes sfSignature
verify(st, HashPrefix::txSign, pubKey);                  // returns bool

// Multi-sign optimization (shared body, per-signer suffix):
auto s = startMultiSigningData(obj);
finishMultiSigningData(signerAccountID, s);  // append signer ID to shared payload
```

`addWithoutSigningFields()` excludes signature fields from the signed payload — this is what breaks the circularity.

### STNumber + STTakesAsset

```cpp
// Vault/Loan/LoanBroker fields use STNumber (no asset embedded).
// In doApply(), after all mutations:
associateAsset(*sle, vaultAsset);  // rounds all sMD_NeedsAsset fields, removes zero-defaults
```

## Critical Files

### Foundations
- `include/xrpl/protocol/SField.h`, `src/libxrpl/protocol/SField.cpp` — field registry, X-macro expansion
- `include/xrpl/protocol/Feature.h`, `src/libxrpl/protocol/Feature.cpp` — `numFeatures`, `FeatureBitset`, registration
- `include/xrpl/protocol/Rules.h` — per-coroutine active amendment set; `isFeatureEnabled()` queries thread-local
- `include/xrpl/protocol/HashPrefix.h` — protocol-immutable domain separators

### Macro Tables (single sources of truth)
- `include/xrpl/protocol/detail/features.macro`
- `include/xrpl/protocol/detail/transactions.macro`
- `include/xrpl/protocol/detail/ledger_entries.macro`
- `include/xrpl/protocol/detail/sfields.macro`
- `include/xrpl/protocol/detail/permissions.macro`

### Type System Roots
- `STBase.h/cpp` — polymorphic root, `emplace()` SOO helper, `JsonOptions`
- `STObject.h/cpp` — heterogeneous container, proxy system, template enforcement
- `STVar` (`detail/STVar.h`) — 72-byte inline variant, depth guard at 10
- `SOTemplate.h/cpp` — schema with O(1) field index; move-only

### Format Registries
- `TxFormats.h/cpp`, `LedgerFormats.h/cpp`, `InnerObjectFormats.h/cpp` — all inherit `KnownFormats<Key, Derived>` with `forward_list<Item>` (pointer-stable) + dual flat_maps

### Amount/Asset Stack
- `Asset.h/cpp` — variant of Issue/MPTIssue; `visit()`, `equalTokens()`, `BadAsset` sentinel
- `Issue.h/cpp`, `MPTIssue.h/cpp` — XRP/IOU and MPT identity
- `STAmount.h/cpp` — unified serialized amount; `canMul`/`canAdd`/`canSubtract` safety checks; `mulRound`/`mulRoundStrict` (legacy vs precise rounding)
- `IOUAmount.h/cpp`, `XRPAmount.h`, `MPTAmount.h/cpp` — lean representations
- `Number` (in `xrpl/basics/`) — high-precision arithmetic; `MantissaRange::large` enabled by SingleAssetVault/LendingProtocol amendments

### Cryptography
- `PublicKey.h/cpp` — 33-byte unified format (0xED prefix for Ed25519); `ECDSACanonicality` enum (canonical vs fullyCanonical), libsecp256k1 normalization
- `SecretKey.h/cpp` — `secure_erase` in dtor; deleted `==`/`<<`; XRPL-specific secp256k1 derivation via `Generator`
- `Seed.h/cpp` — 128-bit; `parseGenericSeed()` cascades hex→base58→RFC1751→passphrase, rejecting other key types first
- `digest.h` — `sha512Half`, `sha512_half_hasher_s` (secure erase variant)
- `tokens.h/cpp` — Base58Check; fast path uses base 58^10 intermediate (10–15× speedup, gated on non-MSVC for `__int128`)

### Wire I/O
- `Serializer.h/cpp` — accumulator; `addVL`, `addFieldID`, big-endian integers, `getSHA512Half()`
- `SerialIter` — non-owning forward cursor over a byte buffer; throws on underrun
- `Sign.h/cpp` — `sign`/`verify` with HashPrefix prepended to `addWithoutSigningFields()` output

### Higher-Level Objects
- `STTx.h/cpp` — caches `tid_` and `tx_type_`; `passesLocalChecks` (memos, pseudo-tx, MPT support, batch nesting); `sterilize()` round-trip
- `STLedgerEntry.h/cpp` (alias `SLE`) — typed ledger object; `thread()` updates `sfPreviousTxnID`; `isThreadedType()` gated by `fixPreviousTxnID`
- `STValidation.h/cpp` — lazy `valid_` cache; `mTrusted` separate from validity; `lookupNodeID` callback decouples manifest system

### Indexes and Keys
- `Indexes.h/cpp` — `keylet::*` factories with `LedgerNameSpace` tagged hashing; `keylet::quality()` embeds 64-bit quality in last 8 bytes (big-endian)
- `Keylet.h/cpp` — type-tagged `(uint256, LedgerEntryType)`; `ltANY` wildcard, `ltCHILD` rejects directories
- NFT pages: composite keys (high 160 = owner AccountID, low 96 = token range); `nft::pageMask` is the boundary

### Validation Helpers (return NotTEC, preflight-time)
- `AMMCore.h/cpp` — `invalidAMMAsset`, `invalidAMMAssetPair`, `invalidAMMAmount`; `ammLPTCurrency()` uses canonical `std::minmax`
- `Permissions.h/cpp` — singleton; `isDelegable()` checks granular vs transaction-level (`<UINT16_MAX` boundary), amendment, delegable flag

### RPC/JSON Boundary
- `STParsedJSON.h/cpp` — depth cap 64; field-path-qualified errors via `make_name`; recognizes `"Payment"`, `"tesSUCCESS"`, etc.
- `ErrorCodes.h/cpp` — append-only enum; `sortedErrorInfos` validated at compile time
- `MultiApiJson.h` — per-API-version `Json::Value` array; composes with `forAllApiVersions` from `ApiVersion.h`
- `jss.h` — every JSON key as `Json::StaticString` via `JSS(name)` macro; PascalCase = protocol fields, snake_case = RPC

### Specialized Types
- `STIssue`, `STAccount` (160-bit, VL-encoded), `STBitString<Bits>`, `STInteger<T>`, `STBlob`, `STArray`, `STVector256`, `STCurrency`, `STPathSet`, `STXChainBridge`, `STNumber` (asset-contextual)
- `Quality.h/cpp` — inverted encoding (lower uint64 = higher quality); `ceil_in`/`ceil_out` proportional scaling; `_strict` variants honor Number rounding mode
- `QualityFunction.h/cpp` — linear `q(out)=m*out+b`; AMMTag (slope from pool) vs CLOBLikeTag (m=0); `combine()` for multi-step strands
- `XChainAttestations.h/cpp` — Attestations:: namespace (full, with signature) vs xrpl:: (stored); `match()` returns three-state `AttestationMatch`

### Pseudo-Account Fields (sMD_PseudoAccount)
- `sfAMMID`, `sfVaultID`, `sfLoanBrokerID` — 256-bit hash representing a synthesized account address

## Numeric Encoding Reference

```
IOU canonical:     mantissa ∈ [10^15, 10^16), exponent ∈ [-96, +80]
                   zero = (mantissa=0, exponent=-100) — sorts below smallest positive
XRP max:           cMaxNativeN = 10^17 drops (100 billion XRP)
MPT max:           maxMPTokenAmount = INT64_MAX = 0x7FFFFFFFFFFFFFFF
Transfer rate:     Rate{value} where value/1_000_000_000 = 1.0 (parityRate = 1:1)
NFT transfer fee:  uint16 basis points (0–50000), convert via nft::transferFeeAsRate (×10000)
```

## Protocol-Stable Constants (NEVER CHANGE)

- `LedgerEntryType` numeric values (in ledger objects)
- `TxType` numeric values (in signed transactions)
- `SerializedTypeID` and `SField` codes (in serialized fields)
- `LedgerNameSpace` discriminator characters (in keylet derivation)
- `HashPrefix` enum values (in signature/hash domain separation)
- `error_code_i` numeric values (clients depend on them; append-only)
- `FLAG_LEDGER_INTERVAL = 256` (drives consensus timing)
- `INITIAL_XRP = 100B × 10^6 drops` (validated by `static_assert` against `Number::maxRep`)
- NFT taxon LCG constants (`384160001 * seq + 2459`)
- All flag bit values (`tf*`, `lsf*`, `asf*`)

Changing any requires an amendment with explicit detection logic for old/new behavior.
