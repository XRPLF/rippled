# `include/xrpl/protocol/Indexes.h` — Ledger Object Address Computation

This header is the single authoritative source for computing the 256-bit addresses of every object in the XRPL ledger state. Any code that needs to read, write, or verify a ledger entry — whether it's an `AccountRoot`, an order-book directory, an NFT page, or a cross-chain bridge object — must go through the functions declared here.

## The Keylet Abstraction

The fundamental return type of this file is `Keylet`, a simple struct pairing a `uint256` key (the object's position in the ledger's SHAMap) with a `LedgerEntryType` enum value. Bundling the type tag with the key is the key design decision: it makes it impossible to look up an offer by using an account root's address and get back the wrong object. The `Keylet::check()` method validates the type against an actual `STLedgerEntry` at retrieval time, providing a runtime assertion against category errors that would otherwise silently corrupt ledger state.

All functions are grouped under the `xrpl::keylet` namespace. Separate free functions below the namespace (`getBookBase()`, `getQualityNext()`, etc.) are explicitly marked deprecated — they predate the keylet system and expose raw `uint256` values without type information.

## Tagged Hashing via `LedgerNameSpace`

The implementation (in `Indexes.cpp`) uses a single internal template, `indexHash(LedgerNameSpace, args...)`, which calls `sha512Half()` with a two-byte namespace prefix prepended to all parameters. The `LedgerNameSpace` enum assigns every ledger object type a unique ASCII character (e.g., `ACCOUNT = 'a'`, `OFFER = 'o'`, `TRUST_LINE = 'r'`). This "tagged hashing" pattern ensures that an `AccountID` used to compute an account root key never collides with the same `AccountID` used to compute an owner directory key, even though both take a single `AccountID` as input. These namespace values are part of the consensus protocol — changing them would permanently relocate every affected object in the ledger, constituting a hard fork.

## Fixed-Key Singletons

Several singleton objects in the ledger (`amendments`, `fees`, `negativeUNL`, and the short-form `skip` list) have no parameters. Their keylets are returned as `Keylet const&` — a reference to a function-local static. This Meyers singleton approach means the hash is computed exactly once at first call and never again, and callers receive a stable reference rather than a by-value copy.

## Symmetric Keys: Trust Lines and AMMs

Both `keylet::line()` (for trust lines / `RippleState` objects) and `keylet::amm()` (for Automated Market Makers) use `std::minmax()` to sort their two account or asset parameters before hashing. A trust line is physically shared between Alice and Bob — there is one object, not two. Without canonical ordering, hashing `(Alice, Bob, USD)` and `(Bob, Alice, USD)` would produce different keys, and one of them would miss the object entirely. The `std::minmax()` canonicalization guarantees that both sides of any bilateral relationship produce the same ledger key.

## Order-Book Quality Embedding

The book and quality keylets use an unusual trick. `keylet::quality(k, q)` takes a directory-node keylet `k` and a 64-bit quality value `q`, then writes `q` in big-endian format into the last 8 bytes of the key by direct pointer manipulation (`((std::uint64_t*)x.end())[-1] = boost::endian::native_to_big(q)`). Because `uint256` keys are compared as big-endian integers in the SHAMap, this embeds the exchange rate directly into the sort key. All offers at adjacent quality levels land at adjacent 256-bit addresses, enabling O(1) iteration from one price level to the next without any secondary index. `keylet::next_t::operator()` adds the constant `0x...0001_0000_0000_0000_0000` to step to the directory for the next quality level.

## NFT Pages: Composite Rather Than Hashed Keys

NFT page keylets (`nftpage_min`, `nftpage_max`, `nftpage`) are conspicuously absent from `indexHash()`. Instead, they assemble a `uint256` by placing the 160-bit `AccountID` in the high bytes and a 96-bit token boundary mask in the low bytes, using `memcpy` and bitwise masking. This design is intentional: the XRPL NFT page structure is a linked list of pages where each page's key encodes its owner and the range of NFT IDs it can hold. The key must compare correctly relative to other pages' keys for range navigation, something that a hash function would destroy. `nftpage()` computes an exact key for the page that would hold a given token by masking the token ID against `nft::pageMask` and ORing it onto the owner prefix.

## Multi-Credential Deposit Preauthorization

The credential-set overload of `keylet::depositPreauth()` handles the case where a deposit is preauthorized for a set of credentials rather than a single account. It takes an already-sorted `std::set<std::pair<AccountID, Slice>>`, hashes each `(AccountID, credentialType)` pair individually into a `uint256`, then hashes the resulting vector under the `DEPOSIT_PREAUTH_CREDENTIALS` namespace — distinct from the simple account-to-account `DEPOSIT_PREAUTH` namespace. This ensures there is no possible key collision between the two preauthorization modes.

## MPT Composite Identifiers

Multi-Purpose Tokens introduce `MPTID` — a 192-bit composite value created by `makeMptID()` that packs a big-endian 32-bit sequence number followed by the 160-bit issuer `AccountID`. The `mptIssuance()` family then hashes this raw ID under the `MPTOKEN_ISSUANCE` namespace. Individual token holdings (`mptoken()`) are keyed by hashing the issuance's ledger key together with the holder's `AccountID` under `MPTOKEN`. This two-level scheme means token balances are naturally grouped under their issuance in the hash space.

## Testing Infrastructure: `keyletDesc` and `directAccountKeylets`

At the bottom of the header sit `keyletDesc<keyletParams...>` and the `directAccountKeylets` array. These are not protocol machinery — they exist purely to drive invariant tests in `Invariants_test.cpp`. `keyletDesc` holds a `std::function` wrapping one keylet factory, the `Json::StaticString` name of the expected ledger entry type, and a boolean flag controlling test inclusion. The `directAccountKeylets` array enumerates every keylet function that accepts a single `AccountID` argument, including `nftpage_min` (noted as normally uncreateable but tested anyway for invariant coverage). New single-`AccountID` keylets should be added to this array so that invariant tests automatically exercise them.