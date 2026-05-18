# `include/xrpl/protocol_autogen/ledger_entries/Credential.h`

## Role in the System

This file is part of the `protocol_autogen` subsystem — a layer of auto-generated, type-safe wrappers over the raw XRPL serialized ledger state. It defines the `Credential` ledger entry (type code `ltCREDENTIAL`, `0x0081`) along with its companion `CredentialBuilder`. The file carries a "do not edit" notice because it is produced by a code generator that walks the XRPL ledger format definitions and emits one header per entry type. Every ledger entry in the `protocol_autogen/ledger_entries/` directory follows the same structural template.

A `Credential` ledger object represents an on-chain verifiable credential: a statement made by an `issuer` account attesting something about a `subject` account. The `credentialType` field is a variable-length blob that acts as an application-defined tag — for example, a KYC-level identifier or a membership class. This is distinct from the earlier `DepositPreauth` mechanism in that a single issuer can issue multiple credential types to multiple subjects, and the credential can carry an optional URI pointing to off-chain metadata.

## The `Credential` Wrapper

`Credential` extends `LedgerEntryBase`, which holds a `shared_ptr<SLE const>` as its sole data member. Const-ness is load-bearing: the pointer is to an immutable `SLE`, so once wrapped, the ledger entry cannot be altered through this interface. This prevents accidental mutation of ledger state through the typed API.

Type safety is enforced eagerly at construction time. The constructor checks `sle_->getType() != entryType` and throws `std::runtime_error` on mismatch. This is the same guard used in every sibling type (e.g. `DID`, `Ticket`): it means that passing a raw `SLE` of the wrong type fails loudly at the wrapping site rather than silently returning garbage values.

The field accessors divide into two categories matching the XRPL `soeREQUIRED` / `soeOPTIONAL` split:

**Required fields** — `getSubject()`, `getIssuer()`, `getCredentialType()`, `getIssuerNode()`, `getPreviousTxnID()`, `getPreviousTxnLgrSeq()` — return values directly. The underlying `SLE::at()` call will assert if the field is absent, which should be unreachable for a validated ledger entry.

**Optional fields** — `getExpiration()`, `getURI()`, `getSubjectNode()` — return `protocol_autogen::Optional<T>`, a typedef defined in `Utils.h`. That alias resolves to `std::optional<T>` when `T` is a value type, or `std::optional<std::reference_wrapper<T>>` when `T` is a reference type. This prevents dangling references for blob-like `SF_VL` fields whose `value_type` may be a const reference into the SLE's internal buffer. Each optional getter is paired with a corresponding `hasXxx()` predicate so callers can distinguish a missing field from a field with a default value without relying on the optional's `has_value()` alone.

`sfIssuerNode` being required while `sfSubjectNode` is optional reflects ledger directory semantics: when a `CredentialCreate` transaction fires, the credential is always threaded into the issuer's owner directory (hence `sfIssuerNode` is always present), but it may also optionally appear in the subject's directory if the subject accepted it — `sfSubjectNode` is the locator for that second directory entry. This asymmetry is baked into the generated type schema.

## The `CredentialBuilder`

`CredentialBuilder` uses the Curiously Recurring Template Pattern (CRTP) through `LedgerEntryBuilderBase<CredentialBuilder>`. The base class holds an `STObject object_{sfLedgerEntry}` as a free (non-templated) object and initialises `sfLedgerEntryType` and `sfFlags`. The CRTP plumbing lets the base class's common setters (`setLedgerIndex`, `setFlags`) return a `CredentialBuilder&` rather than a `LedgerEntryBuilderBase&`, enabling fluent chaining through the derived type.

The constructor enforces required fields by accepting them as positional arguments — subject, issuer, credentialType, issuerNode, previousTxnID, and previousTxnLgrSeq. These six cannot be omitted; optional fields are added post-construction via individual setters. The use of `std::decay_t<typename SF_ACCOUNT::type::value_type>` in setter signatures strips references from the declared field type before taking a const-ref parameter, ensuring that no temporary's lifetime is implicitly extended in a surprising way.

A second constructor accepts a `shared_ptr<SLE const>` and copies the SLE's contents into the builder's internal `STObject` via `object_ = *sle`. This path supports a mutation workflow: read a live entry from the ledger, wrap it in a builder to modify fields, then call `build()` to produce an updated `Credential`. Type enforcement mirrors the read path — an `sfLedgerEntryType` mismatch throws immediately.

The builder deliberately avoids calling `object_.set(soTemplate)` on the internal `STObject`. As the comment in `LedgerEntryBuilderBase` explains, calling `set()` would create placeholder `STBase` objects for `soeDEFAULT` fields, which then trip the `"may not be explicitly set to default"` guard inside `SLE::applyTemplate()` during `build()`. Keeping the object free of default placeholders lets the `SLE` constructor sort it out.

`build(uint256 const& index)` calls `std::make_shared<SLE>(std::move(object_), index)`, consuming the builder's internal state via move, and immediately wraps the resulting SLE in a `Credential`. The move means the builder is not reusable after `build()` — any subsequent use of the builder's setters would operate on a moved-from object, which is an easy invariant to respect since `build()` is the natural terminal call.

## Relationship to the Broader Codebase

The autogenerated test file `CredentialTests.cpp` covers four scenarios: builder setter round-trip, builder-from-SLE round-trip, `Credential` throws on wrong entry type, and `CredentialBuilder` throws on wrong entry type. A fifth test verifies that optional fields return `std::nullopt` when not set. These tests are mechanically generated in the same pass as the header and serve as a contract check rather than behavioural coverage.

The `Credential` type participates in three transaction handlers visible in the codebase: `CredentialCreate`, `CredentialAccept`, and `CredentialDelete`, as well as `DepositAuthorized` — where a credential issued to an account can serve as authorisation evidence when a deposit is gated behind credential requirements.