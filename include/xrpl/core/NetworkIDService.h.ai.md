# `NetworkIDService.h` — Network Identity Interface

`NetworkIDService` is a minimal pure-abstract interface in the `xrpl` namespace that provides read-only access to the integer identifier of the XRPL network this server is configured to operate on. The file is intentionally tiny — one class, one method — because its real purpose is architectural: it decouples the transaction-processing pipeline from the heavyweight `Config` object and makes the network-identity concept independently injectable and testable.

## Why This Interface Exists

The XRPL protocol supports multiple independent ledger networks (mainnet, testnet, devnet, and an open-ended range of custom networks). Transactions submitted to one network must be rejected by all others, or a signed transaction could be replayed across network boundaries. The `sfNetworkID` transaction field enforces this, and validating it requires the node to know its own network ID at transaction-preflight time.

Rather than pulling in a full `Config` reference wherever transaction validation runs, `NetworkIDService` narrows the dependency to the single fact that matters. The `Transactor`'s `preflight0()` function illustrates this directly:

```cpp
uint32_t const nodeNID = ctx.registry.get().getNetworkIDService().getNetworkID();
std::optional<uint32_t> txNID = ctx.tx[~sfNetworkID];
```

The boundary conditions enforced there capture the backward-compatibility story embedded in the well-known ID values:

- **IDs 0–1024 (legacy networks):** The `sfNetworkID` field did not exist when these networks were established. If a transaction *contains* the field, it is rejected with `telNETWORK_ID_MAKES_TX_NON_CANONICAL` — presence of the field signals that the transaction was not built for a legacy network.
- **IDs > 1024 (custom networks):** The field is mandatory. A missing field returns `telREQUIRES_NETWORK_ID`; a mismatched value returns `telWRONG_NETWORK`. This enforces the replay-protection guarantee.

The threshold of 1024 is not arbitrary — it's the protocol's designated boundary between pre-field and post-field networks.

## Concrete Implementation and Lifecycle

The concrete implementation, `NetworkIDServiceImpl` (in `src/xrpld/core/`), stores a `std::uint32_t networkID_` captured at construction time from `config_->NETWORK_ID`. The `Application` class instantiates and owns it:

```cpp
networkIDService_(std::make_unique<NetworkIDServiceImpl>(config_->NETWORK_ID))
```

`Config::NETWORK_ID` itself is parsed from the `[network_id]` section of the rippled configuration file, accepting the string aliases `"main"` (→ 0), `"testnet"` (→ 1), `"devnet"` (→ 2), or any raw integer.

Caching the value at construction rather than reading `Config` on every call is the right trade-off here: the network ID is static for the lifetime of the process, and the cache enables the `noexcept` guarantee on `getNetworkID()`. That guarantee matters because `preflight0()` is called in contexts where exception propagation would be problematic.

## Position in the Service Hierarchy

`NetworkIDService` is one of the core infrastructure services listed in `ServiceRegistry`, which is the application's primary dependency-injection hub. `ServiceRegistry::getNetworkIDService()` returns a reference to the instance, giving any component that holds a `ServiceRegistry` reference a clean path to the network ID without coupling to `Application` or `Config`.

The interface lives in `include/xrpl/core/` (the public `xrpl` library boundary), while `NetworkIDServiceImpl` lives in `src/xrpld/core/` (the private `xrpld` application layer that has access to `Config`). This respects the codebase's layering convention: the protocol-level concept of "a network has an ID" belongs in the public library; the detail of how that ID is read from a config file belongs in the application layer.

This split also enables clean unit testing — test harnesses like `test/jtx/impl/Env.cpp` can supply a stub or mock `NetworkIDService` without any dependency on a real configuration file.