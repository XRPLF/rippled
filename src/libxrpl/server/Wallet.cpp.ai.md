# `src/libxrpl/server/Wallet.cpp`

## Role in the System

`Wallet.cpp` is the persistence layer for a running rippled node's local identity and configuration state. It implements all read and write operations against `wallet.db`, a SQLite database that survives node restarts and holds four categories of data: the node's stable cryptographic identity (its secp256k1 keypair), whitelisted peer reservations, validator and publisher key-rotation manifests, and per-node amendment vote preferences. The file is thin by design — it is purely a data-access layer, deferring all business logic and in-memory caching to higher-level abstractions like `ManifestCache`.

The public surface of this file is declared in `include/xrpl/server/Wallet.h`. The schema it operates against is defined as compile-time string arrays in `include/xrpl/rdb/DBInit.h`: `WalletDBInit` initializes five tables (`NodeIdentity`, `PeerReservations`, `ValidatorManifests`, `PublisherManifests`, and optionally `FeatureVotes`), and `WalletDBName` provides the fixed filename `wallet.db`.

## Database Construction

`makeWalletDB()` and `makeTestWalletDB()` are factory functions that construct a `DatabaseCon` — the XRPL wrapper around a SOCI/SQLite connection — and apply the `WalletDBInit` DDL on first open. The test variant accepts an arbitrary `dbname`, allowing unit tests to use isolated databases without collision. Both return `std::unique_ptr<DatabaseCon>`, transferring ownership to the caller; no manual cleanup is needed.

## Node Identity (`getNodeIdentity`, `clearNodeIdentity`)

`getNodeIdentity()` implements a load-or-generate pattern for the node's stable secp256k1 keypair. On each call it queries `NodeIdentity` and attempts to parse both columns as base58-encoded keys. Critically, it validates that the stored pair is internally consistent by calling `derivePublicKey(KeyType::secp256k1, *sk)` and comparing the result against the stored public key. This guards against a partially-written or corrupted row silently producing a broken identity. Only if the pair passes this check is it returned; otherwise, a fresh random keypair is generated via `randomKeyPair()` and inserted. This means the table may accumulate rows over time if corruption occurs, but the logic always prefers the first valid row found, making the operation idempotent from the caller's perspective. `clearNodeIdentity()` supports test teardown and re-keying scenarios by issuing a simple `DELETE FROM NodeIdentity`.

## Manifest Persistence (`getManifests`, `saveManifests`, `addValidatorManifest`)

The manifest system exists because XRPL validators use a master secret key — kept in cold storage — to sign ephemeral "signing key" certificates. These certificates (manifests) let the rest of the network verify validation signatures without exposing the master key. The wallet database persists manifests across restarts so the node doesn't need to re-gossip them from peers on every boot.

`getManifests()` is the load path: it iterates all rows in the given table, converts each BLOB to a `std::string`, and passes it through `deserializeManifest()` followed by `Manifest::verify()` before calling `mCache.applyManifest()`. Invalid or unverifiable manifests are logged as warnings and skipped — the database is treated as an untrusted source that must be cryptographically re-validated on read.

`saveManifests()` uses a full-replace strategy: it opens a transaction, deletes all existing rows, then re-inserts from the live `ManifestCache` map. This avoids the complexity of a diff. The trust filter is nuanced: untrusted non-revocation manifests are silently dropped (not persisted), but **revocation manifests are always saved regardless of trust status**. This asymmetry is intentional — a revocation is evidence of compromise; discarding it based on trust status would be a security regression.

The low-level `saveManifest()` (file-scope static) creates a fresh `soci::blob` for each row. The comment explains a subtle SOCI quirk: reusing a blob object is unsafe when data lengths vary between inserts, because SOCI's blob write length is expected to be no shorter than the previous write. Since ECDSA signatures vary in length, a new blob per row is the only safe approach.

`addValidatorManifest()` is a focused append — it wraps a single `saveManifest()` call targeting `ValidatorManifests` in its own transaction. It is called when a new validator manifest arrives at runtime (via gossip), rather than during bulk persistence.

## Peer Reservations (`getPeerReservationTable`, `insertPeerReservation`, `deletePeerReservation`)

Peer reservations allow an operator to designate specific nodes that are guaranteed connection slots regardless of the normal peer capacity limits. All three functions are straightforward, but `insertPeerReservation()` is noteworthy for using SQLite's `ON CONFLICT (PublicKey) DO UPDATE SET` upsert syntax — updating the description if the key already exists — rather than a conditional insert. This makes the operation idempotent, which is important since the same node may be re-registered with an updated description. `getPeerReservationTable()` returns an `std::unordered_set` keyed by `PeerReservation` using `beast::uhash`, consistent with the rest of the XRPL codebase's preference for open-addressing hash tables.

## Amendment Voting (`createFeatureVotes`, `readAmendments`, `voteAmendment`)

The `FeatureVotes` table records this node's preferences on XRPL amendments. It is conspicuously absent from `WalletDBInit` — it is created lazily by `createFeatureVotes()`, which checks `sqlite_master` and creates the table only if needed. The return value (`true` if it already existed) signals callers that migration may be required.

The `AmendmentVote` enum has unintuitive integer mappings (up = 0, down = 1, obsolete = -1), explicitly acknowledged in the header comment as a historical artifact that cannot be changed without a migration.

`voteAmendment()` uses an **append-only log** pattern: votes are always inserted, never updated in-place. `readAmendments()` recovers the current state using a window function — `RANK() OVER (PARTITION BY AmendmentHash ORDER BY ROWID DESC)` — selecting only the row with rank 1 (the most recently inserted) per amendment hash. This design preserves the full vote history and avoids `UPDATE` contention at the cost of table growth over time.

## SOCI and `boost::optional`

A recurring pattern throughout the file is the use of `boost::optional` rather than `std::optional` for SOCI output variables. This is a SOCI library requirement — it does not yet support the standard optional — and is called out explicitly in comments at every affected site, serving as documentation for future maintainers who might otherwise "modernize" the code and break it.