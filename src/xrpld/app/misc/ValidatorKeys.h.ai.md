# `ValidatorKeys.h` — Validator Key Material and Manifest Container

## Role in the System

`ValidatorKeys` is a configuration-time data container that parses and holds the validator's cryptographic key material and manifest string as declared in the `rippled.cfg` configuration file. It exists as a bridge between raw configuration text and the typed, validated key objects consumed by the consensus engine (`RCLConsensus`) and the application startup logic. Its construction is the single point where configuration correctness is validated — any misconfiguration is caught here and surfaced through `configInvalid()`, preventing the node from starting.

## Two Configuration Paths

The constructor in `detail/ValidatorKeys.cpp` handles two mutually exclusive configuration sections:

**`[validator_token]` (modern path):** A token encodes an ephemeral signing key and a manifest blob. The constructor calls `loadValidatorToken`, derives the public key from the secret, and then `deserializeManifest` to unpack the manifest. It cross-checks that the derived public key matches the manifest's `signingKey`. If they agree, all three key fields are populated: `masterKey` from the manifest (the stable long-term identity), `pk` as the ephemeral signing key, and `secretKey` from the token. The manifest's `sequence` number is stored to track key rotation, and the raw manifest string is preserved for later submission to the manifest database.

**`[validation_seed]` (legacy path):** A bare seed is parsed; both the master and signing keys are derived identically from it (`keys.emplace(pk, pk, sk)`), and `sequence` is set to 0. There is no separate master vs. signing key distinction — the node signs directly with its permanent identity, bypassing the manifest rotation mechanism.

If both sections are present simultaneously, `configInvalid_` is set immediately and the constructor returns early. This mutual-exclusion guard prevents ambiguous configurations that could cause silent key mismatches.

## The `Keys` Struct Invariant

The nested `Keys` struct enforces an all-or-nothing grouping of `masterPublicKey`, `publicKey`, and `secretKey`. Its constructor is deliberately `delete`d (no default construction), so a `Keys` value can only exist when all three fields are simultaneously provided. The outer `keys` member is `std::optional<Keys>`, which cleanly represents two states: the node is a validator (populated) or it is not (empty). A critical design note embedded in the header itself warns that the *absence* of `keys` must not be conflated with an *invalid* configuration — a node without a `[validator_token]` or `[validation_seed]` section is perfectly valid as a non-validating node.

## `configInvalid_` vs. Empty Keys

These are two orthogonal concerns. `configInvalid_` is set when configuration *attempted* to configure a validator but supplied incorrect data (bad token encoding, public key mismatch, unparseable seed, or contradictory sections). An empty `keys` simply means the node chose not to act as a validator. Callers in `Application.cpp` check `configInvalid()` to gate startup entirely, while they check `keys.has_value()` independently to enable validator-specific behaviors like disallowed ledger tracking and manifest submission.

## How It Flows into Application Startup

`Application` holds a `ValidatorKeys const validatorKeys_` (immutable after construction), initialized with `validatorKeys_(*config_, m_journal)` before any network or consensus subsystems are created. The startup sequence then:

1. Calls `validatorKeys_.configInvalid()` and aborts if `true`.
2. Passes `validatorKeys_.manifest` to `validatorManifests_->load(...)`, inserting this node's own manifest into the manifest store so peers receive it.
3. Passes `validatorKeys_.keys->publicKey` as the `localSigningKey` to `ValidatorList::load`, so the node can recognize its own signing key in trust calculations.

## Role in Consensus

`RCLConsensus::Adaptor` stores a `ValidatorKeys const&` reference throughout its lifetime. Every time the node proposes a consensus position or emits a ledger validation, it checks `validatorKeys_.keys` and, if present, signs the outgoing message with `keys->publicKey` and `keys->secretKey`. The `nodeID` (a hash of the master public key derived via `calcNodeID`) appears in outgoing proposals and validations as the node's stable identity on the network. If `masterPublicKey != publicKey` (i.e., token mode with key rotation), the startup log records both identities and the manifest sequence, providing a clear audit trail of which ephemeral key is currently active.

## Design Trade-offs

Storing the raw `manifest` string rather than a deserialized `Manifest` object keeps this class lightweight and independent of the manifest subsystem's full machinery — the manifest is only needed once during startup to load into the wallet DB. The `sequence` field stored separately avoids re-parsing the manifest at every call site. Using `std::optional<Keys>` rather than null pointers or sentinel values for the key group makes presence testing explicit and type-safe, eliminating an entire class of null-dereference bugs at consensus signing time.