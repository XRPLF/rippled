# `src/libxrpl/server/Manifest.cpp`

## Role in the System

This file implements the XRPL validator key manifest system — a security indirection layer that decouples a validator's long-lived identity (the "master" key) from the ephemeral signing key used day-to-day. The design solves a critical operational problem: if an ephemeral signing key is compromised, the validator operator can issue a new manifest signed by the master key (kept offline) to revoke the old ephemeral key and install a replacement. All peers that receive this manifest immediately stop accepting validations from the old key. If the master key itself is compromised, a revocation manifest with the maximum sequence number (`0xFFFFFFFF`) permanently silences the validator without any further configuration changes being needed across the network.

The file implements `struct Manifest` (the data type), `ManifestCache` (the runtime store), and several free functions for parsing, verifying, and persisting manifests.

## Manifest Structure and Serialization

A `Manifest` carries five fields: a raw serialized byte string (`serialized`), the permanent `masterKey`, an optional `signingKey`, a monotonically increasing `sequence`, and an optional `domain`. The `signingKey` is `std::optional<PublicKey>` because revocation manifests intentionally omit it — a revoked manifest intentionally has no valid signing key on record, so no validations can be accepted from that validator.

`deserializeManifest()` is the front door for all manifest parsing, whether from the network or from config. It declares a local `static SOTemplate` called `manifestFormat` that encodes the schema: `sfPublicKey`, `sfMasterSignature`, and `sfSequence` are required; `sfVersion` defaults to 0; and `sfDomain`, `sfSigningPubKey`, and `sfSignature` are optional. The `STObject::applyTemplate()` call enforces this schema structurally before any business logic runs — required fields missing or type mismatches throw immediately and are caught at the function boundary, returning `std::nullopt`. This prevents any downstream code from dealing with partially-constructed manifests.

After structural parsing, `deserializeManifest` enforces XRPL-specific business rules in a deliberate order. Version forward-compatibility is checked first: only version 0 manifests are understood, and unknown versions are silently rejected rather than treated as errors. The master public key is then validated via `publicKeyType()` to confirm it is a recognized cryptographic key type. Domain strings, if present, are validated by `isProperlyFormedTomlDomain()` to ensure they are well-formed for TOML lookups. Finally, the revocation distinction is enforced: revocation manifests (those with `sequence == 0xFFFFFFFF`) must have neither an ephemeral key nor an ephemeral signature — any such fields would constitute a malformed revocation. Conversely, non-revocation manifests must have both, and the signing key must differ from the master key.

Crucially, **`deserializeManifest` does not verify signatures**. This is documented intentionally in the header. Signature verification is more expensive and is deferred to `Manifest::verify()`, called explicitly after the caller has decided the manifest is worth validating.

## Signature Verification

`Manifest::verify()` re-deserializes from `this->serialized` each call rather than caching a parsed form. This is a deliberate memory tradeoff: storing a pre-parsed `STObject` would duplicate data that is already in `serialized`, and `verify()` is called infrequently. The method verifies two signatures. For non-revocation manifests, it first verifies the ephemeral key's signature over the manifest body using `HashPrefix::manifest` (via `xrpl::verify()`), which zeros out the signature field before hashing so the object signs its own content sans-signature. It then unconditionally verifies the master key's `sfMasterSignature` the same way. A revocation manifest skips the ephemeral signature check (since no ephemeral key exists) and only validates the master signature.

The static and instance `revoked()` overloads are architecturally clean: the static form `Manifest::revoked(uint32_t sequence)` expresses the pure predicate, and the instance form delegates to it. This allows callers to check revocation during deserialization (before a `Manifest` object even exists) and in `ManifestCache` query methods.

## ManifestCache: Thread-Safe Storage with Double-Check Locking

`ManifestCache` maintains two parallel maps: `map_` (master `PublicKey` → `Manifest`) and `signingToMasterKeys_` (ephemeral `PublicKey` → master `PublicKey`). These must be kept consistent — when a manifest is updated, the old ephemeral key is erased from `signingToMasterKeys_` and the new one is inserted. The reverse map enables `getMasterKey()` to answer the common question "given this signing key, who is the validator?" without scanning the entire manifest collection.

`applyManifest()` is the most architecturally interesting function. It uses a deliberate two-phase locking pattern rather than `std::shared_mutex` lock upgrades. The comment in the code explicitly warns against upgradeable locks as a "recipe for deadlock." Instead, the function runs a `prewriteCheck` lambda under a shared read lock first, then re-acquires an exclusive write lock and runs `prewriteCheck` again. The re-run skips the signature check (already validated under the read lock, since cryptographic verification is the expensive step) but repeats all the cheaper consistency checks in case another writer modified the maps between lock acquisitions.

The validation in `prewriteCheck` guards against two categories of security concern beyond simple staleness and signature validity. First, it checks that the manifest's master key is not already in use as an ephemeral key for some other validator — this would indicate a key collision or an attack. Second, it checks that the proposed ephemeral key is not already registered as anyone's master or ephemeral key. Both checks return a distinct `ManifestDisposition` value (`badMasterKey` or `badEphemeralKey`) so the caller can distinguish these failure modes.

The `seq_` field is an `std::atomic<uint32_t>` incremented (without a lock) whenever `map_` changes. External code (such as overlay gossip) polls this value to detect whether new manifests have arrived since the last check, without needing to acquire the mutex for a trivial staleness test.

## Validator Token Loading

`loadValidatorToken()` handles the `[validator_token]` configuration entry. These tokens are multi-line, base64-encoded JSON blobs containing two fields: `manifest` (a base64 manifest string) and `validation_secret_key` (hex-encoded 32-byte raw secret). The function reassembles the multi-line input by trimming whitespace and concatenating, then decodes and JSON-parses it. The `SecretKey` is derived from the raw 32 bytes. The returned `ValidatorToken` pairs the manifest text with the secret key so the caller can both register the manifest in the cache and retain the key for signing validations.

## Persistence

`ManifestCache::save()` holds only a shared read lock while iterating the cache, delegating the actual SQL writes to `saveManifests()` from `xrpl/rdb`. It accepts a caller-provided predicate `isTrusted(PublicKey)` so that only manifests for currently-trusted validators are persisted — untrusted validators' manifests are not worth keeping across restarts. The two-argument `load()` simply calls `xrpl::getManifests()` to populate from the database. The four-argument `load()` additionally processes the `configManifest` and `configRevocation` entries from the node's configuration file, applying them through the same `applyManifest()` code path so that all validation rules are enforced uniformly regardless of where the manifest originated.