# `Clawback.cpp` — Clawback Transaction Transactor

This file implements the `Clawback` transaction type for the XRP Ledger, which allows a token issuer to forcibly reclaim tokens held by another account. The feature is opt-in at the issuer level and is the primary mechanism for regulatory compliance use-cases (e.g., AML/KYC remediation) where an issuer must be able to recover issued tokens. The transactor handles two fundamentally different token models—classic IOU trust-line tokens (`Issue`) and the newer Multi-Purpose Token standard (`MPTIssue`)—within a unified three-phase execution pipeline.

## Transaction Lifecycle and Architecture

`Clawback` extends `Transactor` and participates in the standard three-phase lifecycle: `preflight` (stateless validation), `preclaim` (stateful, read-only validation), and `doApply` (state mutation). Each phase is forwarded to a type-specific template helper via `std::visit` on `sfAmount.asset().value()`, which is a `std::variant<Issue, MPTIssue>`. This means the central dispatcher functions (`preflight`, `preclaim`, `doApply`) are thin routers that extract the token type at runtime and call the correct specialization of `preflightHelper<T>`, `preclaimHelper<T>`, or `applyHelper<T>` via explicit template specialization.

The design cleanly separates the two token models without inheritance or virtual dispatch on the helpers themselves. Each specialization is a file-local `static` function, invisible outside the translation unit.

## IOU Clawback: `Issue` Specializations

**An unusual encoding:** for IOU clawback, the transaction does not use `sfHolder` to identify the token holder. Instead, the holder's account ID is packed into the `issuer` sub-field of the `sfAmount` value (the `STAmount`'s embedded `Issue::account`). This is a deliberate overloading of the field for encoding reasons in the original wire protocol. `preflightHelper<Issue>` explicitly rejects any transaction that also includes `sfHolder`, enforcing mutual exclusivity. At apply time, `applyHelper<Issue>` then corrects this by replacing `clawAmount.get<Issue>().account` with the actual issuer's account before invoking the transfer.

**Permission model for IOUs:** `preclaimHelper<Issue>` enforces two flag conditions on the issuer's account entry: `lsfAllowTrustLineClawback` must be set, and `lsfNoFreeze` must not be set. The `NoFreeze` exclusion is architecturally significant — an account that has permanently waived freeze authority also loses clawback authority. The two flags are mutually exclusive by design, since clawback is an even stronger power than freezing and should not be available to issuers that have made the no-freeze commitment.

**Trust-line balance sign convention:** XRPL trust-line balances are stored with a sign convention tied to account address ordering. A positive raw `sfBalance` means the account with the lexicographically higher address is the net holder. `preclaimHelper<Issue>` explicitly validates this invariant: if the raw balance is positive, the issuer must have the higher address; if negative, the lower address. This check prevents an attacker from constructing a transaction that appears to be clawing back from the correct holder but actually targets the wrong side of the trust line.

**Why `accountHolds` instead of reading the balance directly:** the code comments explain this choice. `accountHolds` accounts for additional constraints on spendable balance (e.g., offers, XLS-34 style lock-ups), whereas the raw `sfBalance` on the trust-line SLE reflects the nominal balance. The preclaim uses `accountHolds` with `fhIGNORE_FREEZE` to verify that the available balance is actually non-zero, and `applyHelper<Issue>` calls it again at apply time to get the current spendable amount, applying `std::min(spendableAmount, clawAmount)` to ensure the transaction never overdrafts even if the ledger state changed slightly between preclaim and apply.

## MPT Clawback: `MPTIssue` Specializations

MPT clawback uses a separate `sfHolder` field in the transaction (the `sfAmount` issuer sub-field is meaningless for MPTs). `preflightHelper<MPTIssue>` requires `featureMPTokensV1` to be enabled and mandates `sfHolder` to be present and distinct from `sfAccount`.

`preclaimHelper<MPTIssue>` checks the MPT issuance object for the `lsfMPTCanClawback` flag, which is set at issuance creation time and cannot be changed later. It also verifies the issuance's `sfIssuer` field matches the transaction submitter, preventing a scenario where someone constructs a transaction against an issuance they don't own. The `MPToken` holder object must exist (`keylet::mptoken`), and `accountHolds` with both `fhIGNORE_FREEZE` and `ahIGNORE_AUTH` is used to determine spendable balance — authorization status is irrelevant for a forced reclaim by the issuer.

The `applyHelper<MPTIssue>` passes `/*checkIssuer*/ false` to `directSendNoFee`, which differs from the IOU path's `true`. This reflects a structural difference: for IOUs, `directSendNoFee` needs to verify issuer involvement in the trust-line accounting; for MPTs the issuance object's ownership is already authoritative.

## Pseudo-Account and AMM Guards

`Clawback::preclaim` includes two protective checks before delegating to the type-specific helpers. First, when `featureSingleAssetVault` is active, `isPseudoAccount` is checked and returns `tecPSEUDO_ACCOUNT` if the holder is a pseudo-account (a vault-managed internal account). Second, regardless of amendment status, accounts with `sfAMMID` present (AMM pool accounts) are blocked with `tecAMM_ACCOUNT`. These guards prevent clawback from being used against protocol-internal accounts that follow different ownership semantics.

## Error Code Summary

The validation logic uses `tem*` codes (stateless malformation) at preflight and `tec*` codes (ledger-state errors) at preclaim, following XRPL conventions. Notable codes: `temBAD_AMOUNT` for zero, XRP, or out-of-range amounts; `temMALFORMED` for missing/unexpected fields; `tecNO_PERMISSION` for missing flags or wrong address ordering; `tecNO_LINE` / `tecOBJECT_NOT_FOUND` for missing ledger objects; `tecINSUFFICIENT_FUNDS` when the spendable balance is zero.