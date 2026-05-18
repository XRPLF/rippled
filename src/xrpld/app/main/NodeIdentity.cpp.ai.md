# NodeIdentity.cpp

`NodeIdentity.cpp` solves a single focused problem: given the running `Application` and its command-line arguments, determine the cryptographic keypair that uniquely identifies this server instance on the XRPL peer network. Every validator and relay node must have a stable identity so that peers can authenticate connections and recognize the node across restarts. This file provides the single function `getNodeIdentity()` that resolves that identity with a clearly-ordered priority chain.

## Resolution Priority

The function implements a three-tier fallback strategy, evaluated in order:

**1. `--nodeid` command-line flag** — The highest-priority override. If the operator passes `--nodeid <seed>` on the command line, `parseGenericSeed()` attempts to decode it as a seed in any recognized format (base58, hex, etc.). A parse failure is fatal: `Throw<std::runtime_error>` with a clear message aborts startup immediately. This path is useful for ephemeral deployments, automated testing environments, or scripted node launches where key material is injected at runtime rather than stored on disk.

**2. `[node_seed]` config file section** — If no command-line override is present but the configuration file contains a `[node_seed]` section, `parseBase58<Seed>()` decodes the first line. This path yields deterministic identity: the same seed always produces the same keypair, so an operator who recorded their seed can reconstruct their node's identity after a database wipe. A malformed seed also throws `std::runtime_error`, preventing startup with a bad configuration.

**3. Wallet database (`WalletDB`)** — If neither override is provided, the function checks out a SOCI database session from the application's wallet database. Before querying, it checks for the `--newnodeid` flag: if present, `clearNodeIdentity(*db)` executes `DELETE FROM NodeIdentity`, intentionally erasing any previously stored identity. Then `getNodeIdentity(*db)` (defined in `src/libxrpl/server/Wallet.cpp`) handles the rest. That function first tries to load and validate an existing row from the `NodeIdentity` table — it confirms the public key genuinely derives from the stored private key before trusting it. If no valid row exists (because the table is empty or the stored keys are inconsistent), it calls `randomKeyPair()` to generate a fresh secp256k1 keypair and inserts it, making the new identity persistent for all future restarts.

## Key Derivation

When a seed is resolved from the command line or config, the derivation is deterministic and always uses `KeyType::secp256k1`:

```
Seed → generateSecretKey(secp256k1, seed) → SecretKey
SecretKey → derivePublicKey(secp256k1, secretKey) → PublicKey
```

The explicit choice of secp256k1 (as opposed to ed25519, which XRPL also supports) is a fixed requirement for node identity. This is distinct from *validator* identity, which uses separate key material and may use different curves.

## Integration Point

The result is consumed immediately in `Application.cpp` (line 1268) during the application startup sequence:

```cpp
nodeIdentity_ = getNodeIdentity(*this, cmdline);
```

After this call, `nodeIdentity_` (a `std::pair<PublicKey, SecretKey>`) is available to the rest of the application — used to sign peer protocol messages and advertise the node's identity during connection handshakes.

## Design Observations

The separation between the high-level `getNodeIdentity(Application&, variables_map&)` in this file and the low-level `getNodeIdentity(soci::session&)` in `Wallet.cpp` is deliberate. The database-level function has no knowledge of configuration or command-line context; it deals only with persistence mechanics. This file owns the policy: which source wins, how errors are reported, and when the database should be reset. The layering keeps the wallet utilities reusable without coupling them to the application bootstrap path.

The `--newnodeid` flag enables deliberate key rotation without editing the config file or manually touching the database. Because the database-level function auto-generates and persists a new keypair when it finds an empty table, the sequence `clear → generate → persist` happens atomically within the same checked-out session, which prevents a partially-initialized state from surviving a crash between steps.