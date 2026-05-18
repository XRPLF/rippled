# `PermissionedDEXHelpers.cpp`

## Role in the System

This file implements the two membership-check predicates that enforce the Permissioned DEX feature in the XRPL: `accountInDomain` and `offerInDomain`. The Permissioned DEX allows a domain owner to create a credential-gated trading environment where only accounts holding specific issued credentials may create or consume offers. These helpers sit at the intersection of the credential system and the DEX, and are invoked both during transaction preclaim validation and at runtime during order book traversal.

## `accountInDomain`

This is the core authorization predicate. Given a `ReadView`, an `AccountID`, and a `domainID`, it resolves the domain's `PermissionedDomain` ledger entry via `keylet::permissionedDomain` and walks its `sfAcceptedCredentials` array.

Two paths lead to membership. First, the domain owner is unconditionally considered to be inside the domain — no credential is required. This implicit membership is intentional: the owner created the domain and cannot be locked out by a missing credential. Second, any other account must hold at least one credential (keyed by `sfIssuer` and `sfCredentialType`) that matches an entry in the domain's accepted list, has the `lsfAccepted` flag set (i.e., the issuer has explicitly accepted the credential on-chain), and has not expired relative to the ledger's `parentCloseTime`. The `std::any_of` scan means a single valid credential is sufficient — the account does not need to satisfy all accepted credential types.

The expiry check delegates to `credentials::checkExpired` from `CredentialHelpers`, which compares the credential's `sfExpiration` field against `view.header().parentCloseTime`. Using the parent close time rather than the current close time is a deliberate XRPL convention — the parent time is finalized and deterministic, while the current close time is not yet committed at preclaim phase.

## `offerInDomain`

This function validates that a specific offer, identified by its `uint256` key, belongs to a given domain. It is designed to be called during order book iteration in `OfferStream`, where the caller already knows the offer's domain from the directory traversal but needs to confirm the offer creator is still eligible for that domain.

The function begins with three structural checks on the offer SLE: it must exist, it must carry an `sfDomainID` field, and that field must match the passed `domainID`. All three are annotated `LCOV_EXCL_LINE`, marking them as defensive branches that should never fire in normal operation because `OfferStream` only calls this function when `entry->isFieldPresent(sfDomainID)` is already true and passes the domain it read from the SLE. The checks exist as a safety net against future caller mistakes.

A fourth structural guard handles hybrid offers. If `lsfHybrid` is set, the offer must also carry `sfAdditionalBooks`. A hybrid offer missing that field is considered malformed, and the function logs an error at `j.error()` before returning `false`. This mirrors the structural invariant checked by `ValidPermissionedDEX::visitEntry` in the invariant-check layer.

After structural validation, `offerInDomain` delegates to `accountInDomain` for the actual credential/membership check on the offer's `sfAccount`. This delegation matters in practice: an offer may have been valid when placed, but the account's credentials can expire or be revoked after the fact. `OfferStream` uses this return value to permanently remove stale domain offers from the book via `permRmOffer`.

## Call Sites and Integration

`accountInDomain` is called from `OfferCreate::preclaim` and `Payment::preclaim`. In `OfferCreate`, it gates the offer creator: if the transaction carries `sfDomainID` but the creator is not in the domain, preclaim returns `tecNO_PERMISSION`. In `Payment`, both the sender and the destination are independently checked — a payment routed through a permissioned domain requires both parties to be members. This prevents a domain offer from being consumed by an ineligible payer or delivering to an ineligible recipient.

`offerInDomain` is used exclusively in `OfferStream`'s offer-iteration loop, which backs both direct DEX consumption and path-based payments. The check occurs after the offer is loaded from the book but before funds are evaluated, ensuring that domain membership is live-verified against the current ledger state rather than relying solely on static order book filtering.

## Error Handling and Invariants

Neither function throws exceptions; both return `bool` and rely on `ReadView`'s returning a null `shared_ptr` for missing ledger entries. The `[[nodiscard]]` attributes on both declarations (in the header) enforce that callers cannot silently ignore the result. The `LCOV_EXCL_LINE` markers are an honest signal about test coverage: the defensive branches in `offerInDomain` are not exercised by the current test suite because their triggering preconditions can only arise from internal inconsistencies in the ledger state, not from any valid user-facing input path.