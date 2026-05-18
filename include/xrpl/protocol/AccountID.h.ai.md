# `include/xrpl/protocol/AccountID.h`

## Purpose and Role

`AccountID.h` defines the fundamental identity type for XRPL accounts. Every participant in the XRP Ledger — wallets, issuers, escrow destinations, multisig signers — is addressed by an `AccountID`. This header establishes the type, its serialization contract, two protocol-level sentinel constants, a performance cache, and the JSON integration hook. It is included pervasively throughout the rippled codebase wherever accounts appear.

## The Type: Phantom Tagging on `base_uint`

```cpp
using AccountID = base_uint<160, detail::AccountIDTag>;
```

`AccountID` is not a bare `typedef` — it is a distinct strong type. `base_uint<Bits, Tag>` uses its second template parameter as a phantom tag, so `base_uint<160, AccountIDTag>` and a hypothetical `base_uint<160, SomeOtherTag>` are entirely separate types and cannot be mixed silently. `AccountIDTag` itself is an empty class in `namespace xrpl::detail`, existing only to instantiate this uniqueness. This design catches at compile time the class of bug where a raw 160-bit hash is accidentally used as an account address or vice versa.

The underlying `base_uint` stores 160 bits as five `uint32_t` values in big-endian byte order — a layout that is part of the XRP Ledger's binary serialization protocol and cannot be changed without a hard fork. The type inherits iterator support (acting as a byte-range container), `beast::zero` comparison, hex parsing via `parseHex()`, and `hardened_hash<>` as its default hasher.

## Deriving an AccountID from a Public Key

`calcAccountID()` (declared in `PublicKey.h`, implemented in `AccountID.cpp`) applies the SHA-256 + RIPEMD-160 pipeline to a public key's raw bytes:

```cpp
ripesha_hasher rsh;
rsh(pk.data(), pk.size());
return AccountID{static_cast<ripesha_hasher::result_type>(rsh)};
```

The code comments include a direct quote from David Schwartz explaining why this specific combination was chosen: XRPL inherited it from Bitcoin to avoid giving critics a reason to claim it was less secure, since RIPEMD-160 is considered safe at 160 bits and the double-hash avoids length-extension vulnerability. It was a deliberate conservative choice, not an independently derived cryptographic decision.

## Serialization: Base58Check with TokenType

XRPL uses a Base58Check encoding that is *not* identical to Bitcoin's — it uses a different alphabet and prepends a `TokenType` version byte. For accounts, `TokenType::AccountID` has value `0`. `toBase58()` calls `encodeBase58Token(TokenType::AccountID, ...)` and `parseBase58<AccountID>()` is a full template specialization that calls `decodeBase58Token`, then validates the decoded payload is exactly 20 bytes — a hard rejection of anything malformed. The `std::nullopt` return on parse failure (rather than an exception) reflects the expectation that input from external sources is frequently untrusted.

`to_issuer()` is a deprecated convenience that accepts either a 40-character hex string or a base58 string, used in legacy configuration parsing. Its continued presence is a migration artifact.

## Performance Cache

Base58Check encoding requires a SHA-256 checksum computation on every call, which is expensive when processing thousands of transactions. `initAccountIdCache()` allocates a flat open-addressing cache of `CachedAccountID` entries, each holding an `AccountID` and a 40-char encoding buffer:

```cpp
auto const index = hasher_(id) % cache_.size();
packed_spinlock sl(locks_, index % 64);
```

The cache uses `hardened_hash<>` — a seeded hash — specifically to resist hash-flooding attacks where a crafted workload could degrade a naive modulo hash to O(n) collision chains. Fine-grained locking uses 64 spinlocks packed bitwise into a single `std::atomic<uint64_t>` via `packed_spinlock`, eliminating per-entry lock memory overhead while still allowing concurrent access to different cache buckets.

One subtle defensive guard appears in the cache lookup:

```cpp
if (cache_[index].encoding[0] != 0 && cache_[index].id == id)
```

The `encoding[0] != 0` check prevents the all-zero `AccountID` (the XRP sentinel) from matching against a zero-initialized empty slot, since zero-initialized memory would otherwise look like a valid cache hit for `xrpAccount()`.

The cache is strictly optional — if `initAccountIdCache(0)` is called or it was never initialized, `toBase58()` falls through directly to `encodeBase58Token`. The cache initializes exactly once; subsequent calls to `initAccountIdCache` are no-ops.

## Sentinel Constants

`xrpAccount()` returns the all-zero `AccountID` (backed by `beast::zero`), used as the canonical "issuer" for native XRP in amount fields. `noAccount()` returns `AccountID(1)`, a placeholder representing the absence of a meaningful account (for example in uninitialized offer or trust line fields). Both are function-local statics returned by `const&`, making them lazily initialized singletons without global constructor ordering issues.

`isXRP()` tests whether an account ID equals `beast::zero`, essentially asking whether an amount's issuer is XRP rather than a token. It is marked deprecated because callers should prefer checking the currency or native flag directly — relying on the zero-account-as-issuer convention is a leaky abstraction.

## JSON Integration

The `Json::getOrThrow<xrpl::AccountID>()` specialization lives in `namespace Json` alongside the type, bridging the ledger's JSON API into the type system. It reads a string field, parses it as base58, and throws `JsonTypeMismatchError` on failure — the same error type thrown for any other JSON type mismatch. This is the mechanism by which RPC handlers and transaction parsers convert incoming JSON account strings into `AccountID` values without writing repetitive parsing boilerplate.

## Hash and Deprecation Notes

`std::hash<AccountID>` is specialized to delegate to `AccountID::hasher` (which is `hardened_hash<>`), but is itself marked deprecated. New code should use `beast::uhash` or XRPL's hardened unordered containers directly. The presence of the `std::hash` specialization maintains compatibility with code that passes `AccountID` to standard library containers, while steering new code toward attack-resistant alternatives.