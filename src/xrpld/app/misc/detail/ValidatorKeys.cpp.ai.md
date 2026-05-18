# `ValidatorKeys.cpp` — Validator Identity Bootstrap from Configuration

This file implements the single constructor of `ValidatorKeys`, the class responsible for loading a validator node's cryptographic identity at startup. It bridges the raw configuration file and the rest of the validator machinery by parsing, validating, and materialising the key material that the node will use to sign ledger validations.

## Role in the Validator System

An XRPL validator node has a dual-key identity. A long-lived **master key** certifies the validator's identity; a short-lived **ephemeral signing key** is what actually signs each validation message. The indirection exists so that if an ephemeral key is compromised the operator can rotate it — generating a new signing key and a new **manifest** (an XRPL-specific certificate signed by the master key) — without every peer needing to update its trusted-key list. The manifest system is documented in detail in `include/xrpl/server/Manifest.h`.

`ValidatorKeys` is the object that carries the resolved result of this bootstrapping: the master public key, the current signing public key, the corresponding signing secret key, the raw base64 manifest string, the manifest sequence number, and the node ID derived from the master key. All downstream code (the manifest cache, the validation signing path, the peer handshake) obtains these values from a single `ValidatorKeys` instance constructed once at application startup.

## Two Configuration Paths

The constructor supports two mutually exclusive configuration sections, differentiated at parse time:

**`[validator_token]`** is the modern, production path. The operator generates an offline master key pair and uses it to produce a `ValidatorToken` blob — a small JSON object containing a base64-encoded manifest and a hex-encoded ephemeral secret key. `loadValidatorToken()` (defined in `Manifest.cpp`) decodes this JSON, extracts the raw 32-byte secret, and returns a `ValidatorToken` struct. The constructor then derives the public key from that secret via `derivePublicKey(KeyType::secp256k1, token->validationSecret)` and cross-checks it against the signing key embedded in the deserialized manifest (`pk != m->signingKey`). This check is the critical correctness invariant: it confirms that the secret key in the token and the manifest it accompanies are internally consistent, preventing a misconfiguration where an operator pastes mismatched components. On success the `Keys` struct is populated with `(m->masterKey, pk, token->validationSecret)`, clearly distinguishing master from signing identity.

**`[validation_seed]`** is the legacy, non-manifest path. The operator provides only a base58-encoded seed. The constructor derives the secret key with `generateSecretKey` and the public key with `derivePublicKey`, then constructs `Keys` with `(pk, pk, sk)` — deliberately collapsing master and signing key into the same value. Sequence is left at zero and no manifest string is stored, signalling to callers that there is no manifest indirection in use. This path remains functional for development and test setups but offers no key-rotation capability.

If both sections are present simultaneously the constructor sets `configInvalid_` and returns immediately with a fatal log, before any key material is touched. This explicit mutual-exclusion guard prevents ambiguous configuration that could silently favour one section over the other.

## Error Signalling Without Exceptions

The constructor never throws. Any validation failure — unparseable token, manifest that cannot be deserialized, a secret key whose derived public key doesn't match the manifest's signing key, or an invalid base58 seed — sets `configInvalid_ = true` and emits a `j.fatal()` log entry, then returns with `keys` left as `std::nullopt`. The caller is responsible for checking `configInvalid()` before using the object. The header comment on `keys` explicitly warns against using its presence as a proxy for validity: a node may have a well-formed configuration with no validator identity at all (i.e., running as a non-validator), in which case neither section is present and `keys` is empty but `configInvalid_` remains false.

## Key Relationships

- `ValidatorToken` (from `Manifest.h`) is a plain struct holding `manifest: std::string` and `validationSecret: SecretKey`. The constructor moves `token->manifest` into `ValidatorKeys::manifest` to avoid a copy of a potentially large base64 string.
- `deserializeManifest()` returns `std::optional<Manifest>` and does not verify the manifest signature itself; that responsibility sits with `Manifest::verify()` and the `ManifestCache`, which will later call `applyManifest()` using the raw manifest string stored in `ValidatorKeys::manifest`.
- `calcNodeID(m->masterKey)` in the token path and `calcNodeID(pk)` in the seed path both derive the `NodeID` from the **master** public key — ensuring the node's peer-network identity is tied to the stable long-term key, not the rotatable ephemeral one.
- `SECTION_VALIDATOR_TOKEN` and `SECTION_VALIDATION_SEED` are string macros defined in `ConfigSections.h`, mapping to the `[validator_token]` and `[validation_seed]` headings in `rippled.cfg`.