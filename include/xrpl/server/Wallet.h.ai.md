# `include/xrpl/server/Wallet.h` — Wallet Database Interface

This header defines the persistence layer for a rippled node's local operational state: its cryptographic identity, validator manifests, peer reservations, and amendment vote preferences. Despite its name, this has nothing to do with cryptocurrency wallets in the user-facing sense. The "wallet" is a SQLite database (typically `wallet.db`) that stores the small set of configuration state that must survive restarts but doesn't belong in the consensus ledger itself.

## What Gets Stored

The wallet database is the authority for four distinct categories of node-local state, each backed by its own SQL table:

**Node identity** (`NodeIdentity` table): A secp256k1 keypair that uniquely identifies this node on the peer-to-peer network. `getNodeIdentity()` implements lazy generation — it reads the existing keypair from the database, validates that the public key correctly derives from the private key, and only generates a fresh random keypair if none is found or if the stored pair fails that consistency check. Once generated, the identity is written back immediately so it persists across restarts. `clearNodeIdentity()` erases it, forcing regeneration on the next call.

**Validator manifests** (`ValidatorManifests` and `NodeManifests` tables): Binary-encoded certificates that allow validators to rotate their ephemeral signing keys without changing their stable master public key identity. `getManifests()` loads raw blobs from a named table, deserializes each with `deserializeManifest()`, verifies the cryptographic signature, and feeds valid manifests into a `ManifestCache`. `saveManifests()` does the inverse: it wraps the operation in a transaction, deletes all existing rows, then re-inserts only manifests belonging to trusted validators plus all revocation manifests regardless of trust. The full-delete-before-insert pattern (rather than upsert) ensures the stored set stays clean when trust configuration changes.

**Peer reservations** (`PeerReservations` table): A named allowlist of peer nodes by public key, used to guarantee connection slots for specific peers even under load. `insertPeerReservation()` uses SQLite's `ON CONFLICT DO UPDATE` upsert — a different strategy than manifest saving, appropriate here because individual entries are mutable in place. `getPeerReservationTable()` returns the full set as an `unordered_set<PeerReservation, beast::uhash<>, KeyEqual>`, matching the in-memory type used by `PeerReservationTable`.

**Amendment votes** (`FeatureVotes` table): Records of the operator's preferences on XRPL protocol amendments. The design here has an interesting quirk: `voteAmendment()` always INSERTs a new row rather than updating an existing one, so the table can accumulate multiple entries per amendment hash. `readAmendments()` resolves this with a SQL window function — `RANK() OVER (PARTITION BY AmendmentHash ORDER BY ROWID DESC)` — selecting only the most recent vote per amendment. This append-only approach means a vote history is retained, but only the latest entry is ever acted upon. The `AmendmentVote` enum carries a notable comment: the integer representations are historically inverted (`up = 0`, `down = 1`), a legacy inconsistency that was never corrected. `createFeatureVotes()` conditionally creates the table and returns `true` if it already existed, letting callers distinguish first-time setup from re-initialization.

## Database Setup

`makeWalletDB()` constructs and opens the database by passing `WalletDBName` (a constant from `DBInit.h`) and `WalletDBInit` (the array of SQL `CREATE TABLE IF NOT EXISTS` statements) to `DatabaseCon`. This makes schema initialization automatic on first open. `makeTestWalletDB()` takes an explicit database name, enabling test cases to open isolated instances without colliding with each other or with production state.

## Session Design

All functions accept a raw `soci::session&` rather than a `DatabaseCon&` or `LockedSociSession`. This is a deliberate layering choice: callers obtain a `LockedSociSession` (which holds the mutex) via `DatabaseCon::checkoutDb()`, then dereference it to pass into these functions. The wallet functions themselves are stateless — they execute SQL against whatever session they receive. This avoids re-locking within the same thread (which would deadlock given `DatabaseCon`'s `recursive_mutex`) and keeps the functions usable in test contexts where callers control session lifetime directly.

The `boost::optional` types scattered throughout the SOCI calls (rather than `std::optional`) reflect a SOCI library constraint noted in code comments: SOCI's output binding API predates C++17 and requires Boost's optional type.

## Relationship to `ManifestCache`

The wallet functions sit one layer below `ManifestCache`. The cache's own `load()` and `save()` methods delegate to `getManifests()` and `saveManifests()` respectively, passing their internal `hash_map<PublicKey, Manifest>`. The wallet header therefore acts as the SQL binding layer for manifest persistence, while `ManifestCache` handles the in-memory concurrency model (a `shared_mutex` protecting its two hash maps).