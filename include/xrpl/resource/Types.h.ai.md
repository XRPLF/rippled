# `Types.h` — Resource Subsystem Forward Declarations

Within the `xrpl::Resource` namespace, this file serves as the central forward-declaration header for the two core internal data structures that underpin the resource management subsystem: `Key` and `Entry`.

## Role and Purpose

The resource management subsystem tracks load imposed by network endpoints — both inbound client connections and peer-to-peer overlay connections — and applies warnings or disconnections when consumption exceeds configured thresholds. Internally, the subsystem revolves around two structs:

- **`Key`** (defined in `detail/Key.h`): identifies a unique consumer endpoint, combining a `Kind` classification (inbound, outbound, or admin) with a `beast::IP::Endpoint` network address. It carries nested `hasher` and `key_equal` types so it can serve directly as the key in `hash_map` containers inside `Logic`.

- **`Entry`** (defined in `detail/Entry.h`): the per-endpoint tracking record. It holds an exponentially decaying `DecayingSample` for local resource consumption, a normalized `remote_balance` contributed by cluster gossip, reference counts from active `Consumer` handles, and timestamps for the last warning and expiry of inactive entries. It also back-references the `Key` it was inserted under.

## Design Decision: Forward Declarations Over Full Includes

`Types.h` exposes only forward declarations — `struct Key;` and `struct Entry;` — keeping the full definitions confined to the `detail/` subdirectory. This is a deliberate layering choice: public-facing headers such as `Consumer.h` and `ResourceManager.h` need only to name these types (in pointer or reference position) without requiring their complete definitions, which would drag in heavy dependencies like `beast::IP::Endpoint`, `DecayingSample`, and the intrusive list machinery. The forward-declaration-only header is the lightweight bridge that makes this separation compile cleanly.

In practice, `Consumer.h` carries its own inline `struct Entry;` forward declaration rather than including `Types.h`, and `Logic.h` pulls in the full `detail/Entry.h` and `detail/Key.h` definitions transitively. This suggests `Types.h` functions as an explicit, canonical declaration point — a statement of what the internal types *are* at the namespace level — rather than an actively included dependency chain anchor.

## Relationship to the Broader Module

The resource module is intentionally structured in two visibility tiers. The public API (`Consumer`, `ResourceManager`, `Charge`, `Disposition`, `Fees`) exposes the rate-limiting interface to the rest of `xrpld`. The `detail/` headers hold the implementation machinery. `Types.h` sits at the boundary, giving a name to the two structs that connect those tiers without blurring the separation.