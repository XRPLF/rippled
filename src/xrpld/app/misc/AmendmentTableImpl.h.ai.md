# `AmendmentTableImpl.h`

This header exists as the factory boundary between the abstract `AmendmentTable` interface (defined in `xrpl/ledger/AmendmentTable.h`) and its concrete implementation. It declares a single factory function, `make_AmendmentTable`, that returns a fully-constructed implementation hidden behind a `std::unique_ptr<AmendmentTable>`. Callers never need to know the implementation type, and the translation unit containing the implementation can be compiled independently.

## Why a Separate Factory Header?

The pattern mirrors several other components in this directory: `FeeVote.h` exposes `make_FeeVote`, and `NegativeUNLVote` follows the same convention. The rationale is architectural layering: the `AmendmentTable` interface lives in the `xrpl/ledger` layer — a lower-level, application-agnostic library — while the concrete `AmendmentTableImpl` class lives here in the application layer. Keeping them separate prevents the lower-level ledger library from pulling in application-layer concerns (like `ServiceRegistry`), while still allowing the application to instantiate the concrete object at startup.

Notably, the factory signature is declared in *two* places: once here in `AmendmentTableImpl.h` (the app-layer include), and once again at the bottom of `xrpl/ledger/AmendmentTable.h` itself. The latter declaration means code that only depends on the interface header can still call the factory, enabling the linker to resolve the definition from `detail/AmendmentTable.cpp` without requiring an explicit include of this implementation header. The `Impl` suffix in the filename signals that this header belongs to the implementation side of the split.

## `make_AmendmentTable` Parameters

The parameters collectively encode the full runtime policy for amendment governance:

- **`ServiceRegistry& registry`**: Provides access to application-wide services. In `Application.cpp`, the application itself (`*this`) is passed here, as it implements `ServiceRegistry`. This allows the implementation to register listeners for ledger close events and consensus callbacks without coupling to the full application type.

- **`std::chrono::seconds majorityTime`**: The continuous duration a supermajority of validators must support an amendment before it is scheduled for activation. In practice this is set from `config().AMENDMENT_MAJORITY_TIME`. Injecting it rather than hardcoding it allows test suites to use an artificially short interval and allows the parameter to be tuned at deployment without recompilation.

- **`std::vector<AmendmentTable::FeatureInfo> const& supported`**: The set of amendments this node's compiled software understands, each carrying a name, a 256-bit hash ID, and a `VoteBehavior` indicating the node's default voting stance. Amendments unknown to the node are neither supported nor vetoed — they are simply invisible to the voting logic, which is the safe default for forward compatibility with future amendments.

- **`Section const& enabled` / `Section const& vetoed`**: Configuration file sections (from `[amendments]` and `[veto_amendments]` respectively) declaring operator overrides. The two-section separation matters: an operator can suppress a node's vote for a supported amendment via `vetoed` without claiming the amendment doesn't exist, and can force-enable a local amendment override via `enabled`. The implementation parses these sections with a hex-ID + name regex to extract `uint256` amendment identifiers.

- **`beast::Journal journal`**: Standard XRPL structured logging sink, injected to allow the implementation to emit diagnostics under the `"Amendments"` log category.

## Relationship to Sibling Files

The concrete class `AmendmentTableImpl` is defined entirely within `detail/AmendmentTable.cpp` — its class definition does not appear in any header. `make_AmendmentTable` at the end of that file simply calls `std::make_unique<AmendmentTableImpl>(...)` and returns it as the base-class pointer. This opaque-implementation approach means changes to the internal `TrustedVotes` anti-flapping mechanism, the `AmendmentState` per-amendment struct, or any other internal detail require recompiling only the implementation translation unit, not the entire application.

At runtime, the single `AmendmentTable` object constructed here is the central authority for all amendment lifecycle management: it tallies validator votes (via `doVoting`), tracks majority windows (suppressing transient flapping via the `TrustedVotes` class), detects unsupported-but-enabled amendments that put the node at risk of falling out of consensus, and serves the `feature` RPC endpoint through its `getJson` overloads.