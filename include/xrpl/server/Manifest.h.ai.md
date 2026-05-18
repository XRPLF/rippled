# `include/xrpl/server/Manifest.h`

## Role in the System

This header defines the two-layer validator key infrastructure that allows XRPL validators to rotate their signing keys without requiring a network-wide configuration change. Without this mechanism, a compromised validator signing key would force every node on the network to update its UNL (Unique Node List) with the new public key simultaneously — a severe operational and security problem. The manifest system introduces an offline master key that issues signed certificates ("manifests") delegating signing authority to an online ephemeral key. The master key can be kept in cold storage, while the ephemeral key does the real-time work of signing validations.

## The `Manifest` Struct

`Manifest` holds the deserialized representation of a single validator certificate. Its fields capture the logical content of the certificate: `masterKey` (the long-lived identity key), `signingKey` (the current online key, absent on revocation manifests), a monotonically increasing `sequence` number, an optional `domain` string for validator identification, and the raw `serialized` bytes that include the digital signatures.

Copy construction and copy assignment are deliberately deleted. Moving a manifest is cheap and safe (the serialized blob stays contiguous in memory); copying it would create ambiguity about which copy is authoritative, and there is no use case that requires it.

The `verify()` method does two rounds of signature verification on the deserialized object. For non-revocation manifests it first verifies the ephemeral key's signature over the manifest body (`sfSignature`), then verifies the master key's signature (`sfMasterSignature`). A revocation manifest skips the ephemeral key check because no signing key is present. The implementation re-deserializes `serialized` to an `STObject` on each call rather than caching the parse result — this keeps the struct small and avoids extra lifetime concerns.

Revocation is encoded as the sentinel sequence number `0xFFFFFFFF` (`std::numeric_limits<std::uint32_t>::max()`). The choice of using the maximum value as the sentinel is elegant: it is impossible to supersede a revocation manifest because no higher sequence number exists. Both the static `revoked(uint32_t)` and the instance `revoked()` method expose this check; the static form lets callers test a candidate sequence number during deserialization before constructing the full `Manifest` object.

## Deserialization

`deserializeManifest` is a family of three overloads (accepting `Slice`, `std::string`, or `std::vector<char/unsigned char>`) all funneling through the `Slice` variant. The implementation uses an inline `static SOTemplate` that defines the XRPL binary encoding schema for a manifest: mandatory `sfPublicKey`, `sfMasterSignature`, and `sfSequence`; optional `sfSigningPubKey`, `sfSignature`, and `sfDomain`; plus a `sfVersion` field that gates forward compatibility (only version 0 is currently understood).

The function deliberately separates parsing from signature verification, returning a valid `Manifest` object without having checked the signatures. The caller must separately invoke `Manifest::verify()`. This design makes sense because the `ManifestCache` wants to perform the expensive cryptographic check only under a lightweight read lock, deferring the result-based write to a separate exclusive lock acquisition — an optimization that would be impossible if deserialization automatically verified.

The domain string is validated against `isProperlyFormedTomlDomain()` during deserialization, preventing manifests with syntactically invalid domain claims from ever entering the system. The sanity check that the signing key and master key cannot be identical (`*signingKey == masterKey`) prevents a degenerate manifest where the two key layers collapse into one.

## `ValidatorToken`

`ValidatorToken` is a minimal bootstrap struct that pairs the local validator node's manifest (as a base64-encoded string) with its `SecretKey` for signing validations. It is loaded from the `[validator_token]` config entry via `loadValidatorToken()`, which base64-decodes the blob and parses it as JSON with `manifest` and `validation_secret_key` fields. This is the mechanism by which a validator operator configures their node — the manifest within the token is added to the local `ManifestCache` and the secret key is used to sign live validation messages.

## `ManifestDisposition`

This enum encodes the five outcomes of attempting to add a manifest to the cache: `accepted`, `stale` (sequence not strictly greater than what's known), `badMasterKey` (master key is already used as someone else's ephemeral key), `badEphemeralKey` (ephemeral key reused across validators), and `invalid` (cryptographic verification failed). The string conversion `to_string(ManifestDisposition)` supports structured logging of these outcomes without string-based branching at call sites.

## `ManifestCache`

`ManifestCache` is the runtime registry that maps validator master public keys to their current best manifest. It maintains two indexes: `map_` keyed by master public key (giving quick access to the current signing key and manifest metadata), and `signingToMasterKeys_` keyed by ephemeral signing key (providing the reverse lookup, used by `getMasterKey()`). This bidirectional structure allows the overlay code to receive a validation signed by an ephemeral key and efficiently resolve it back to the master key identity that appears on the trust list.

The concurrency design deserves close attention. `applyManifest()` implements a double-checked locking pattern without an upgradable lock. It first runs `prewriteCheck` under a `std::shared_lock`, including the expensive `Manifest::verify()` call. If that passes, it releases the read lock and acquires a `std::unique_lock`, then re-runs `prewriteCheck` (without the signature check, since it has already passed) to guard against racing writers in the window between the two locks. The code comments explicitly note that an upgradable lock was considered and rejected as a deadlock risk — a correct choice given that `std::shared_mutex` does not support lock upgrades without releasing.

The atomic `seq_` counter is a lightweight change-detection signal. It is incremented each time `applyManifest` successfully installs a new or updated manifest. External callers (e.g., `ValidatorList`) can snapshot this value and check it cheaply to detect whether new manifest data has arrived since they last synchronized, without holding the mutex. This avoids polling under lock for what is essentially a notification problem.

The two `for_each_manifest` template overloads acquire a `shared_lock` for the duration of their iteration, explicitly warning callers not to call any `ManifestCache` member functions from within the callback (which would attempt to re-acquire `mutex_` from the same thread, causing undefined behavior). The two-parameter overload allows a caller to first receive the total count via `pf(map_.size())` before iterating — useful for pre-allocating output buffers without two separate lock acquisitions.

`load()` and `save()` handle persistence against the database. On startup, the full overload of `load()` first reads all previously persisted manifests from the database table, then applies the locally configured `[validator_token]` manifest, then applies any `[validator_key_revocation]` entry. This ordering ensures database-cached gossip manifests don't accidentally win over the operator's own current configuration.