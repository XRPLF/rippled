# `include/xrpl/protocol/LedgerFormats.h`

This header is the authoritative registry for every object type that can live in the XRP Ledger. It simultaneously defines three separate, tightly-coupled things: the numeric type identifiers stored inside ledger objects, the flag bitmasks that modify their behavior, and the `LedgerFormats` singleton that knows the serialized field layout of every object type. All three are protocol-level constants — changing any of them without careful amendment machinery would cause a hard fork.

## `LedgerEntryType` — Object Type Identifiers

The `LedgerEntryType` enum (`uint16_t`) assigns a stable integer ID to each on-ledger object type. These IDs are embedded in every serialized ledger object and are used during ledger iteration to determine an object's type and to verify that a hash lookup returned the expected kind of object.

The concrete type values are generated via the `ledger_entries.macro` X-macro, which is included twice in the codebase with a different `LEDGER_ENTRY` definition each time. Here the macro expands to `tag = value,` to populate the enum. This single-source-of-truth pattern ensures the same set of (tag, value, name, fields) tuples is used consistently for enum membership, format registration in the constructor, and auto-generated API output.

Beyond the macro-generated members, two sentinel pseudo-types appear manually:

- `ltANY = 0` — used in keylet lookups where any object type is acceptable. Objects cannot be created with this type.
- `ltCHILD = 0x1CD2` — used in keylets where the object must not be a directory node but the precise type is irrelevant.

Three legacy entries (`ltNICKNAME`, `ltCONTRACT`, `ltGENERATOR_MAP`) are retained as `[[deprecated]]` members rather than removed. The comment explains the rationale: even though these IDs were never used for real objects, deleting them from the enum would open their numeric slots for accidental reuse by future types. Keeping them marked deprecated ensures the compiler warns on any new usage while the slot remains claimed in the protocol namespace.

The header's `@todo` acknowledges a gap: C++ enums cannot enforce uniqueness of values at compile time, so duplicate IDs can silently coexist. This is a known risk given the wide numeric gaps in the assigned values.

## `LedgerSpecificFlags` — Flag Bitmasks via Nested X-Macros

Flag definitions use a more elaborate multi-level X-macro strategy. The core table is the `XMACRO` macro, which lists every ledger object type alongside its named flags and their hex values. Three local helper macros (`TO_VALUE`, `NULL_NAME`, `TO_MAP`, etc.) are then applied to the same `XMACRO` to generate three distinct outputs in sequence:

**1. The `LedgerSpecificFlags` enum** (`uint32_t`): `XMACRO(NULL_NAME, TO_VALUE, NULL_OUTPUT)` strips the object names and collects all flag names and values into a single flat enum. This is the enum code uses directly (`lsfRequireDestTag`, `lsfGlobalFreeze`, `lsfSellNFToken`, etc.).

A subtle detail is `LSF_FLAG2` versus `LSF_FLAG`. `lsfMPTLocked` has the same bit value `0x00000001` in both `MPTokenIssuance` and `MPToken`. Emitting it twice via `LSF_FLAG` would create a duplicate enum value, which is well-defined in C++ but triggers warnings. `LSF_FLAG2` maps to `NULL_OUTPUT` in the enum pass, silently omitting the second occurrence from the enum while still including it in the per-object flag maps.

**2. Per-object flag accessor functions**: `XMACRO(TO_MAP, VALUE_TO_MAP, VALUE_TO_MAP)` expands into one inline function per ledger object type — `getAccountRootFlags()`, `getOfferFlags()`, `getRippleStateFlags()`, and so on. Each returns a `const LedgerFlagMap&` (i.e., `std::map<std::string, uint32_t>`), initialized once via Meyer's singleton. This avoids the static-initialization-order fiasco while providing efficient repeated access.

**3. `getAllLedgerFlags()`**: The outermost aggregator collects all per-object maps into a `vector<pair<string, LedgerFlagMap>>` using the same singleton pattern. This function is the sole data source for the `server_definitions` RPC endpoint, which exposes the full flag catalogue to external API consumers.

The entire macro block is bracketed by `#pragma push_macro` / `#pragma pop_macro` guards, protecting any prior definitions of the internal macro names (`XMACRO`, `TO_VALUE`, etc.) in case the header is included in a translation unit that has already defined them for its own purposes.

## `LedgerFormats` — Format Registry

`LedgerFormats` inherits from `KnownFormats<LedgerEntryType, LedgerFormats>` using CRTP. `KnownFormats` is the generic engine: it maintains a `std::forward_list<Item>` (node-stable storage so pointer identity is preserved), a flat name map, and a flat type map. `LedgerFormats` fills it in its constructor.

The constructor (defined in `LedgerFormats.cpp`) performs the second expansion of `ledger_entries.macro`, this time with `LEDGER_ENTRY` defined as a call to `add(jss::name, tag, UNWRAP fields, getCommonFields())`. This registers each entry type's `SOTemplate` — the schema describing which serialized fields are required, optional, or default — alongside its name and numeric type ID.

`getCommonFields()` returns the three fields shared by every ledger object: `sfLedgerIndex` (optional), `sfLedgerEntryType` (required), and `sfFlags` (required). These are injected as common fields into every `SOTemplate` via `KnownFormats::add`, so each entry type's schema automatically includes them without being listed individually in the macro.

`getInstance()` is a Meyer's singleton returning the one global `LedgerFormats`. Code throughout `rippled` — particularly `STLedgerEntry`, `STParsedJSON`, and invariant checkers — calls it to retrieve the `SOTemplate` for a given `LedgerEntryType`, validate incoming serialized objects, or iterate all known types.

The `LEDGER_ENTRY_DUPLICATE` macro exists to handle the name-collision problem: `DepositPreauth` names both a transaction type and a ledger entry type. The JSS system (which generates JSON string constants) cannot have two `JSS(DepositPreauth)` expansions in the same translation unit. `LEDGER_ENTRY_DUPLICATE` expands identically to `LEDGER_ENTRY` in the constructor context but can be given a no-op definition in JSS-generating contexts to skip the second occurrence.

## Design Tradeoffs

The overarching design decision is to make the `ledger_entries.macro` file the single source of truth for all per-type data. This means the enum, the constructor, the format schemas, and any future generated output (auto-generated builder classes exist in `protocol_autogen/`) all derive from the same table, making it impossible for them to drift apart. The cost is that the macro layer adds cognitive overhead: understanding what the header actually declares requires mentally expanding two separate macro passes.

Flag values start at `0x00010000` for most object types (leaving the lower 16 bits for future use or system purposes), but `DirNode`, `NFTokenOffer`, and the MPToken family use the lower bits — a legacy of their respective feature design decisions. This inconsistency is visible but harmless as long as the flags are only interpreted in the context of a known object type.