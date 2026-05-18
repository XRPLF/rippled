# `NetworkIDServiceImpl.cpp` — Concrete Network Identity Provider

This file provides the sole concrete implementation of the `NetworkIDService` abstract interface, giving the rest of the XRPL node a single, stable point of access to the numeric network identifier.

## Role in the Service Architecture

`NetworkIDService` (defined in the public `include/xrpl/core/` tree) declares one pure virtual method: `getNetworkID() const noexcept`. Keeping the interface in the public include tree while hiding `NetworkIDServiceImpl` inside `src/xrpld/` is deliberate — components that only need to *query* the network ID depend solely on the thin abstract base; only `Application.cpp`, which owns the concrete wiring, needs to know about the implementation class.

## Immutable-by-Design Cache

The constructor accepts a `std::uint32_t networkID` and stores it directly into the private member `networkID_`. There is no setter and no post-construction mutation path. This is appropriate because network identity is a launch-time property: a running node cannot switch networks without restarting. By resolving the value once — from `Config::NETWORK_ID`, which itself parses the `network_id` config file section and maps well-known aliases (`"main"` → 0, `"testnet"` → 1, `"devnet"` → 2, or any raw integer for custom networks ≥ 1025) — `NetworkIDServiceImpl` guarantees every caller gets the same answer for the lifetime of the process.

## `noexcept` Contract

`getNetworkID()` propagates the `noexcept` guarantee declared on the virtual method. Because it only returns a stored integer this is trivially safe, but the explicit annotation matters: call sites involved in transaction validation and signing (e.g., `TransactionSign.cpp`, `Transactor.cpp`) may be invoked in contexts where exception propagation is undesirable.

## Construction Site

`Application.cpp` constructs the service as `std::make_unique<NetworkIDServiceImpl>(config_->NETWORK_ID)` and stores it as a `unique_ptr<NetworkIDService>`, enforcing the interface-only dependency for all downstream consumers.