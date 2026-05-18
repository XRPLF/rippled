# `NodeIdentity.h` — Node Cryptographic Identity Interface

This header declares the single entry point for resolving a rippled server's cryptographic identity: the secp256k1 key pair that uniquely identifies the node on the XRP Ledger peer-to-peer network. Every node, whether validator or relay, needs a stable public/private key pair so that peers can authenticate connections and recognize the same node across restarts.

## The `getNodeIdentity` Function

```cpp
std::pair<PublicKey, SecretKey>
getNodeIdentity(Application& app, boost::program_options::variables_map const& cmdline);
```

The function takes both the running `Application` — giving access to the configuration file and wallet database — and the parsed command-line arguments. It returns a `std::pair<PublicKey, SecretKey>` representing the resolved identity. `Application::nodeIdentity_` is populated from this return value during startup in `Application.cpp`.

The implementation in `NodeIdentity.cpp` enforces a clear three-tier priority chain:

1. **`--nodeid` command-line flag**: The seed is parsed via `parseGenericSeed()`. This is the highest priority and useful for scripted or ephemeral deployments where the identity should not be persisted.
2. **`[node_seed]` config file section** (`SECTION_NODE_SEED`): The value is parsed as a Base58-encoded `Seed`. This supports deterministic static identity via config, common for validators that want a fixed, operator-controlled key.
3. **Persistent wallet database**: If neither override is present, the function falls back to `getNodeIdentity(*db)` from `<xrpl/server/Wallet.h>`, which reads or auto-generates a stable secp256k1 keypair stored in the local SQLite wallet database.

The `--newnodeid` command-line flag triggers `clearNodeIdentity(*db)` before the database lookup, forcing generation of a fresh keypair — the mechanism for intentional key rotation.

## Design Rationale

The header keeps the interface minimal and the concerns separated. The priority logic sits entirely in `NodeIdentity.cpp`, while the raw database operations (`getNodeIdentity(soci::session&)`, `clearNodeIdentity(soci::session&)`) are delegated to `<xrpl/server/Wallet.h>`. This means `NodeIdentity.h` is the application-layer facade: it owns the *selection policy* for which identity source takes precedence, not the mechanics of storage or generation. Invalid seeds at either the command-line or config level throw `std::runtime_error` immediately, failing fast at startup rather than propagating a broken identity into the running node.