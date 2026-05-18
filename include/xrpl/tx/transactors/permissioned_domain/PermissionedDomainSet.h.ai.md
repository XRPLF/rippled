# `PermissionedDomainSet.h` — Transactor for Creating and Modifying Permissioned Domains

## Role in the System

`PermissionedDomainSet` is the transactor responsible for both **creating** a new `PermissionedDomain` ledger object and **modifying** an existing one. Unlike many XRPL transaction types that separate creation and mutation, this single transactor serves dual purposes: the presence or absence of `sfDomainID` in the transaction body determines which path executes. The class lives alongside `PermissionedDomainDelete` in the `permissioned_domain` module — the two together form the full lifecycle management for the feature.

Permissioned Domains are an XRPL construct that lets an account define a named set of accepted credentials (issuer + credential type pairs). Other protocol features can then scope access to holders of those credentials, rather than having to enumerate individual accounts.

## Class Interface

`PermissionedDomainSet` publicly inherits `Transactor` and declares `ConsequencesFactory{Normal}`, meaning it uses the standard fee/consequence model with no blocking behavior. The constructor merely forwards `ApplyContext&` to the base.

The lifecycle follows the framework's three-phase pipeline — `checkExtraFeatures`, `preflight`, `preclaim`, and `doApply` — dispatched by the template `Transactor::invokePreflight<T>` at the infrastructure layer. Only `doApply` is an actual virtual override; the static methods participate in compile-time polymorphism via name hiding, not vtable dispatch.

## Transaction Lifecycle

**`checkExtraFeatures`** gates the entire transaction on the `featureCredentials` amendment. If the amendment is not enabled in the current ruleset, `invokePreflight` returns `temDISABLED` immediately without entering `preflight`. This is notable because `PermissionedDomainDelete`, the sibling transactor, does *not* define its own `checkExtraFeatures` — it inherits the base class no-op that always returns `true`. This asymmetry presumably reflects that deletion is a safe cleanup operation that should remain available even if the feature gate were ever conditionally lifted.

**`preflight`** performs stateless validation against the transaction fields only. It delegates to `credentials::checkArray` to verify that `sfAcceptedCredentials` respects `maxPermissionedDomainCredentialsArraySize` and that each credential entry is structurally sound. It also rejects any transaction that presents `sfDomainID` equal to `beast::zero` — a zero hash is not a valid domain identifier and would indicate a malformed client submission.

**`preclaim`** adds read-only ledger state checks against the current view. It confirms the submitting account actually exists (a guard against internal inconsistency), then iterates every credential in `sfAcceptedCredentials` to verify each `sfIssuer` account is present on ledger — a domain referencing a non-existent issuer would be permanently unresolvable. For update operations (where `sfDomainID` is present), it reads the domain SLE directly and verifies both existence (`tecNO_ENTRY`) and ownership (`tecNO_PERMISSION`), preventing any account from modifying a domain they don't own.

**`doApply`** is where the actual ledger mutation happens. It first sorts the incoming credentials via `credentials::makeSorted`, converting them into a canonical ordering before writing to the SLE. This normalization ensures ledger objects always store credentials in a deterministic sequence regardless of submission order, which matters for equality checks and hash stability.

The method then branches on the presence of `sfDomainID`:

- **Update path**: Peeks the existing domain SLE, replaces its `sfAcceptedCredentials` array in-place with the sorted result, and calls `view().update()`. No reserve change occurs — this is a pure field mutation.

- **Create path**: First checks that the submitting account has sufficient XRP balance to cover the incremented owner reserve (`accountReserve(ownerCount + 1)`). The new `PermissionedDomain` SLE is keyed by `keylet::permissionedDomain(account_, sequence)` — the transaction sequence number is embedded in the domain's identity, making each created domain globally unique without requiring a separate ID generation mechanism. The SLE is inserted into the account's owner directory, `sfOwnerNode` is populated with the returned page index, and `adjustOwnerCount` increments the reserve counter by one before inserting the object.

## Design Decisions

The use of `sfSequence` as the domain key differentiator is idiomatic XRPL: since sequence numbers are monotonically increasing and unique per account, `(account, sequence)` is a collision-free domain identifier derivable at creation time without any ledger lookup. This also means the `sfDomainID` used in subsequent transactions is deterministic and auditable — it can be recomputed from the creating transaction's metadata.

The sorted credential storage reflects a broader XRPL pattern of canonical normalization on write rather than on read. Because multiple parties may query or hash the same domain object, keeping the stored representation deterministic avoids subtle divergence between nodes. The sort happens inside `doApply` rather than in `preflight` or `preclaim` to minimize repeated work on transactions that ultimately don't apply.

Error codes follow the `tec`/`tef`/`tem` hierarchy strictly: structural problems (malformed fields) return `tem` codes from `preflight`, missing-but-queryable state returns `tec` codes from `preclaim`, and invariant violations that "should never happen" return `tefINTERNAL` guarded by `LCOV_EXCL_LINE` annotations that acknowledge those paths are not reached in normal testing.