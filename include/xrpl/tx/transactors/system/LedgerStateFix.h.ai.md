# `LedgerStateFix.h` — Ledger State Repair Transactor

## Role and Purpose

`LedgerStateFix` implements the `ttLEDGER_STATE_FIX` transaction type, a protocol-level maintenance mechanism that allows network participants to submit a transaction that corrects corrupted or inconsistent ledger state. It lives in `include/xrpl/tx/transactors/system/`, alongside peers like `Change` (validator/fee settings) and `Batch`, all of which perform protocol-level operations rather than ordinary value transfers.

The class is gated on the `fixNFTokenPageLinks` amendment and currently supports a single fix operation: repairing broken linkage between NFToken page objects in an account's NFToken directory. The design uses an extensible `FixType` enum (`uint16_t`) to allow new repair operations to be added in future amendments without changing the transaction's overall structure.

## Key Design Decisions

**Dispatch via `FixType` enum.** The transaction carries a required `sfLedgerFixType` field (a `uint16_t`) that selects which repair operation to perform. All three lifecycle methods — `preflight`, `preclaim`, and `doApply` — switch on this value. Adding a new fix type in a future release means extending the `FixType` enum and adding a new case in each method. The `default` branch in each switch returns `tefINVALID_LEDGER_FIX_TYPE` (preflight) or `tecINTERNAL` (preclaim/doApply), with the latter two lines marked `// LCOV_EXCL_LINE` because preflight is supposed to prevent any unknown `FixType` from reaching those stages. This layered defense ensures that if preflight logic is ever broken, the subsequent stages still fail safely.

**Owner-reserve fee, not base fee.** `calculateBaseFee()` delegates to `Transactor::calculateOwnerReserveFee()` rather than returning the network base fee. This is the same pricing strategy used by `AccountDelete`. The elevated fee (one owner reserve) serves as an economic guardrail: it deters frivolous invocations and ensures only accounts with genuinely broken state are worth the cost to fix. The caller pays this fee and it is non-refundable even if the repair finds nothing to correct.

**`sfOwner` is conditionally required.** The transaction definition marks `sfOwner` as `soeOPTIONAL` at the protocol level to keep the schema flexible for future fix types. However, `preflight()` enforces that it is present when `FixType::nfTokenPageLink` is the selected operation, returning `temINVALID` otherwise. This way, future fix types that target a global or non-account resource need not carry a superfluous owner field.

**`ConsequencesFactory{Normal}`.** This declaration tells the transaction queue that `LedgerStateFix` is not a blocker — it does not prevent other transactions from the same account from being processed. This is appropriate because the repair operation does not consume sequence numbers in a way that blocks subsequent queue entries.

## Lifecycle

`preflight` validates the `sfLedgerFixType` field and, for the `nfTokenPageLink` type, confirms that `sfOwner` is present. No amendment check is needed here because `invokePreflight` handles amendment gating via `checkExtraFeatures` and the permission registry before calling this method.

`preclaim` reads the ledger view to confirm the target account identified by `sfOwner` actually exists (`keylet::account(owner)`), returning `tecOBJECT_NOT_FOUND` if not. This check happens at claim time rather than preflight time because ledger state can change between when a transaction is submitted and when it is applied.

`doApply` calls `nft::repairNFTokenDirectoryLinks(view(), ctx_.tx[sfOwner])`, which walks the account's NFToken page chain and corrects any broken forward/backward links between pages. If the helper returns `false` (no repair was possible or an error occurred), `doApply` returns `tecFAILED_PROCESSING`; otherwise it returns `tesSUCCESS`.

## Relationship to Related Files

The implementation (`LedgerStateFix.cpp`) depends on `xrpl/ledger/helpers/NFTokenHelpers.h` for `nft::repairNFTokenDirectoryLinks`, which contains the actual page-link traversal and repair logic. The auto-generated `xrpl/protocol_autogen/transactions/LedgerStateFix.h` provides the type-safe `LedgerStateFix` wrapper and `LedgerStateFixBuilder` used by clients constructing these transactions; the builder enforces the required `sfLedgerFixType` at construction time. The transactor header itself is deliberately minimal: it declares only what the dispatch framework needs — the `FixType` enum, the `ConsequencesFactory` tag, and the four lifecycle methods — while all repair logic stays in the `.cpp`.