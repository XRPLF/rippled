# `NetworkIDServiceImpl.h` — Concrete Network Identity Provider

`NetworkIDServiceImpl` is the private, application-layer implementation of the public `NetworkIDService` interface. Its entire purpose is to capture the `network_id` value from the application config at startup and serve it as a cheap, lock-free read for the lifetime of the process.

## Interface Layering

The abstract base `NetworkIDService` lives in `include/xrpl/core/` — the public library boundary — and declares a single pure virtual method `getNetworkID() const noexcept`. This separation is deliberate: the concept that "a network has an ID" is a protocol-level concern that any consumer can depend on without knowing anything about config parsing. The concrete `NetworkIDServiceImpl`, however, lives in `src/xrpld/core/` (the private `xrpld` application layer), keeping the config-reading detail out of the public interface.

## Construction and Ownership

The constructor takes a `std::uint32_t networkID` directly rather than a `Config` reference, so the caller (`Application.cpp`) pre-extracts the value via `config_->NETWORK_ID`:

```cpp
networkIDService_(std::make_unique<NetworkIDServiceImpl>(config_->NETWORK_ID))
```

`Application` stores the result as `unique_ptr<NetworkIDService>`, so all downstream consumers see only the abstract interface. The value is immutable after construction — the private `networkID_` member is set once in the member-initializer list and never touched again — making `getNetworkID()` trivially thread-safe without any synchronization.

## Design Choices

Marking the class `final` signals that this is a leaf node in the hierarchy; the polymorphism lives entirely in `NetworkIDService`. The `noexcept` qualifier on `getNetworkID()` is significant: it allows callers in hot paths (transaction validation, routing) to call it without exception bookkeeping. The XRPL network ID namespace assigns 0 to mainnet, 1 to testnet, 2 to devnet, and 1025+ to custom networks — the last range is important because custom-network transactions are required to carry an explicit `NetworkID` field, and this service is the canonical source for validating that field.