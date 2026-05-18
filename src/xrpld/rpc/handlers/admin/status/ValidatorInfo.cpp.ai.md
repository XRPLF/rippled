# `ValidatorInfo.cpp` — Admin RPC Handler for Local Validator Identity

## Role in the System

This file implements `doValidatorInfo`, an admin-only RPC handler that reports the local node's own validator key configuration back to the operator. It answers a specific operational question: "what validator identity is this node currently using, and what manifest is active?" This is distinct from the general-purpose `Manifest.cpp` handler in `server_info/`, which looks up manifests for *any* validator by accepting a public key as a request parameter. `doValidatorInfo` takes no parameters — it always describes the local node's own state, and it is only accessible via the admin API.

## The Two-Key Validator Architecture

Understanding this handler requires understanding how XRPL validator keys work. Validators use a two-tier key scheme described in `include/xrpl/server/Manifest.h`. A **master key** is kept offline under strict access control and is never used to sign ledger validations directly. An **ephemeral (signing) key** is installed on the live validator and signs validations in production. The master key signs a **manifest** — essentially a certificate — that binds the master key to the current ephemeral key, along with a sequence number and optional domain claim. This lets operators rotate the hot signing key (e.g., after a suspected compromise) without requiring every other node on the network to update their config: they just receive a new manifest via gossip, verify it against the already-known master key, and replace the old ephemeral key.

`ManifestCache` (`ManifestCache` in `Manifest.h`) is the in-memory store for these manifests, indexed by master public key and reverse-indexed by ephemeral key. Its `getMasterKey()` method has a crucial identity-fallback behavior: if the supplied key is not found as a signing (ephemeral) key in any manifest, it returns the key itself unchanged. This fallback is the mechanism `doValidatorInfo` uses to detect the no-manifest case.

## Control Flow and Design Decisions

The handler first calls `context.app.getValidationPublicKey()`, which returns `std::optional<PublicKey>`. This method (`Application.cpp` line 537) reads `validatorKeys_.keys->publicKey` — the ephemeral signing key parsed from the node's `[validator_token]` config entry. If the node has no validator key configured at all, it returns an empty optional, and the handler immediately returns `RPC::not_validator_error()`.

When a key is present, the handler passes it to `getValidatorManifests().getMasterKey(*validationPK)`. Because the node stores its own key as the *ephemeral* key, `getMasterKey` returns the master key from the manifest. If `mk == validationPK`, the fallback identity was triggered: the node has no manifest in the cache (or is running in a simplified key mode where the validation key is also the master key). In this case the handler returns early with only `master_key` populated. The early return is intentional — `ephemeral_key`, `manifest`, `seq`, and `domain` are only meaningful when there is an active manifest relationship between two distinct keys.

When master and ephemeral keys differ, the handler populates the remaining fields by querying the same `ManifestCache` through four separate optional-returning accessors: `getManifest()`, `getSequence()`, and `getDomain()`. Each is independently guarded with `if (auto const ...)`, meaning any field may be absent without being an error. In practice, `manifest` and `seq` should always be present when a manifest exists, but the optional pattern defends against transient cache states or future schema changes. The raw manifest bytes returned by `getManifest()` are binary-serialized XRPL objects, so they are `base64_encode`d for JSON transport.

## Encoding Conventions

Both master and ephemeral keys are formatted with `toBase58(TokenType::NodePublic, ...)`, producing the `n...` prefixed base58check strings familiar in XRPL validator configuration. The manifest blob itself is base64 because it is a binary serialized XRPL `STObject`, not human-readable text. The `jss::` namespace constants (`master_key`, `ephemeral_key`, `manifest`, `seq`, `domain`) are compile-time string constants ensuring JSON field name consistency across the codebase.

## Relationship to Sibling Handlers

The `admin/status/` directory groups read-only diagnostic handlers requiring admin authorization. `Validators.cpp` (a two-line handler) delegates entirely to `ValidatorList::getJson()` for the *set* of trusted validators from the UNL. `ValidatorInfo.cpp` is complementary: it reports only the node's own identity within that set. The parallel public-facing `server_info/Manifest.cpp` serves external callers who need to inspect any validator's manifest by submitting a public key — it does not check whether the node is itself a validator.