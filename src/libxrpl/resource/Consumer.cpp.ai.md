# `Consumer.cpp` — Resource Consumption Handle

`Consumer.cpp` implements the `xrpl::Resource::Consumer` class, the public-facing handle through which every network endpoint (inbound peer, outbound peer, or administrative connection) interacts with the XRPL resource management system. Its job is simple to describe but nuanced to get right: it is a reference-counted, copyable proxy to a shared `Entry` in the central `Logic` table, exposing an API for charging load costs, querying disposition, and triggering warnings or disconnections.

## Role in the Resource Subsystem

The resource subsystem protects the rippled node from abuse by tracking per-endpoint resource consumption over time using an exponentially decaying balance (`DecayingSample`). When consumption crosses a warning threshold, the server notifies the peer; when it crosses the drop threshold, the server disconnects it. `Consumer` is the object that sits on the network layer side of this boundary — every connection object holds one, and every RPC handler that applies a fee calls `Consumer::charge()`.

The real work lives in `Logic`, a class that owns the authoritative `hash_map<Key, Entry>` table and enforces all thresholds. `Consumer` is deliberately thin: it delegates every meaningful operation to `Logic` and exists primarily to manage `Entry` lifetime through reference counting.

## Reference Counting and Lifetime

The most architecturally significant aspect of `Consumer` is its copy semantics. An `Entry` record in `Logic::table_` tracks a `refcount` representing how many `Consumer` objects are actively pointing to it. When the copy constructor or `operator=` fires, it calls `m_logic->acquire(*m_entry)`, incrementing the count under `Logic`'s `recursive_mutex`. When a `Consumer` is destroyed or reassigned away, it calls `m_logic->release(*m_entry)`, which decrements the count. If the count hits zero, `Logic::release()` moves the `Entry` from the appropriate active list (`inbound_`, `outbound_`, or `admin_`) to the `inactive_` list, sets an expiry timestamp, and leaves the actual hash map erasure for `Logic::periodicActivity()` to handle.

This deferred erasure is a deliberate design choice: it means the node retains consumption history for recently-disconnected peers, preventing them from immediately reconnecting with a clean slate after a drop. The raw pointer `m_entry` is therefore always stable and non-dangling for the lifetime of any `Consumer` that holds a reference, because the entry is only erased from the map when no `Consumer` references it and enough time has elapsed.

The private constructor `Consumer(Logic& logic, Entry& entry)` is only accessible to `Logic` (declared as `friend`), ensuring that `Consumer` objects can only originate from a valid, centrally-managed entry. Default-constructed `Consumer` objects (with both `m_logic` and `m_entry` as `nullptr`) act as an explicit null/empty state for cases where a consumer is not yet assigned.

## Two Tiers of Null Safety

The member functions deliberately split into two safety tiers:

**Soft guards** (nullptr checks that return safe defaults): `to_string()`, `isUnlimited()`, `disposition()`, and `charge()` all perform explicit `nullptr` checks. A default-constructed or moved-from `Consumer` can survive calls to these functions without crashing. `charge()` adds a third guard: `!m_entry->isUnlimited()`. Unlimited (administrative) endpoints bypass the charge path entirely, making the cost of checking nearly zero for trusted connections.

**Hard guards** (assertions): `warn()`, `disconnect()`, `balance()`, and `entry()` use `XRPL_ASSERT` to assert that `m_entry` is non-null. These methods are only called by connection-handling code that has already validated that the `Consumer` is bound to a real endpoint, so a null here represents a programming error rather than a runtime condition.

## Querying Disposition Without Side Effects

`disposition()` is subtly clever: it calls `m_logic->charge(*m_entry, Charge(0))` — a charge of zero cost. `Logic::charge()` adds the fee cost to the entry's `DecayingSample` and then evaluates the resulting balance against the warning and drop thresholds to return a `Disposition`. Passing zero doesn't alter the balance, so `disposition()` gets a fresh threshold evaluation with no side effects. This is cheaper and more correct than duplicating the threshold logic in `Consumer`, and it ensures that the same clock-relative balance calculation used by real charges is used for disposition queries.

## `setPublicKey` and Identity

`setPublicKey()` bypasses `Logic` entirely and writes directly to `m_entry->publicKey`. This is safe because `publicKey` is purely identification metadata used in `Entry::to_string()` (via `getFingerprint`). It is never consulted in resource calculations or threshold comparisons. The design reflects a pragmatic layering: endpoints don't always know their peer's public key at the moment a `Consumer` is created (it's learned during the handshake), so the key is patched in afterward.

## Relationship to Logic and Entry

`Logic` is the true owner and authority. `Consumer` is a vocabulary type that makes the resource API ergonomic for callers — they get a copyable value type that can be stored in connection state, passed around, and destroyed without manual reference management. The relationship mirrors `std::shared_ptr` semantics without using atomic operations: all reference count mutations go through `Logic`'s `recursive_mutex`, which also guards the table, the intrusive lists, and the balance calculations, making the whole system coherently thread-safe under concurrent connection activity.