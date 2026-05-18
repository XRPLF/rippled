# `include/xrpl/ledger/helpers/TokenHelpers.h`

## Role in the System

This header is the unified dispatcher layer for all token operations that must work across XRPL's three asset classes: XRP, IOU (trust-line-based), and MPT (Multi-Party Token, introduced later). It sits between transaction-processing code that wants to be asset-agnostic and the two type-specific implementation modules: `RippleStateHelpers.h` for IOU trust lines, and `MPTokenHelpers.h` for `MPToken`/`MPTokenIssuance` objects. Callers pass an `Asset` — a `std::variant<Issue, MPTIssue>` — and this module dispatches via `std::visit` or `Asset::visit` to the correct lower-level function, returning consistent result types (`STAmount`, `TER`, `bool`) regardless of asset kind.

The design follows the same "shotgun surgery" avoidance pattern used elsewhere in the helpers directory: adding a new asset type only requires extending the `Asset` variant and adding a branch in these dispatchers, not touching every call site.

## Enum Vocabulary

Five enums establish a precise vocabulary for policy decisions that otherwise degrade into `bool` parameters:

- `FreezeHandling` (`fhIGNORE_FREEZE` / `fhZERO_IF_FROZEN`) — whether to treat a frozen balance as zero or as its actual value. A caller interested in "what is legally spendable" uses `fhZERO_IF_FROZEN`; a cleanup path that needs to see the real balance regardless uses `fhIGNORE_FREEZE`.
- `AuthHandling` (`ahIGNORE_AUTH` / `ahZERO_IF_UNAUTHORIZED`) — parallel to freeze handling but for MPT authorization.
- `SpendableHandling` (`shSIMPLE_BALANCE` / `shFULL_BALANCE`) — for IOUs, the "full" balance includes the opposite side's credit limit (so an account that has borrowed can still spend up to that limit), and for the issuer it returns the issuance's `MaximumAmount − OutstandingAmount`.
- `WaiveTransferFee : bool` — whether the transfer fee is skipped. Using `enum class` over raw `bool` prevents accidental transposition at call sites.
- `AllowMPTOverflow : bool` — MPT `OutstandingAmount` can temporarily exceed `MaximumAmount` during payment-engine routing (because the redeeming leg will cancel the overshoot); this flag allows that transient state. Direct sends (`directSendNoFee`) use `No`, while `accountSend` via the payment engine uses `Yes`.

`AuthType` (`StrongAuth`, `WeakAuth`, `Legacy`) encodes a subtle three-way distinction:
- `StrongAuth` checks that the holding object itself exists before asking whether authorization is set.
- `WeakAuth` skips the existence check, returning success if authorization isn't required at all even when no holding exists.
- `Legacy` maps to `StrongAuth` for MPT and `WeakAuth` for IOU, preserving historical behavior at existing call sites.

## Freeze Checking

The freeze surface is deliberately layered. `isGlobalFrozen` checks the issuer's account-level flag that freezes all holders simultaneously. `isIndividualFrozen` checks the specific trust line or `MPToken` object for a single account. `isFrozen` combines both. `isAnyFrozen` accepts an initializer list so an offer's two sides (taker and maker) can be checked with one call.

For MPT, "deep freeze" (`isDeepFrozen`, `checkDeepFrozen`) is conceptually identical to `isFrozen`, because MPT semantics prohibit frozen accounts from both sending and receiving. For IOU, deep freeze is a distinct state (the `lsfDeepFreeze` trust-line flag) where the account cannot send but can still receive. The separate function family avoids conflating these semantics despite the MPT no-op.

Both `isFrozen` and `isDeepFrozen` carry a `depth` parameter for recursive vault checking. Vaults can hold MPT shares backed by other assets; if those assets are frozen the shares should be treated as frozen too. The recursion is bounded by `maxAssetCheckDepth` and is described as "purely defensive" — the ledger does not currently allow such nested vaults to be created, so depth > 0 should not occur in practice.

`checkFrozen` converts the boolean result to a `TER`, but importantly returns `tecFROZEN` for IOU and `tecLOCKED` for MPT. These are distinct error codes with distinct meanings in protocol responses, so the dispatching `checkFrozen(…, Asset const&)` overload delegates to the typed overloads rather than performing its own mapping.

## Balance Queries

`accountHolds` answers "what can this account actually spend right now?" The four overloads form a funnel: the most specific (by `Currency`/`AccountID` pair) implements the real logic; the `Issue`, `MPTIssue`, and `Asset` overloads are all adapters.

For XRP the answer is `xrpLiquid(…)` — the reserve-adjusted liquid balance. For IOU with `shFULL_BALANCE` and `account == issuer`, the function short-circuits to `STAmount::cMaxValue` because the issuer is always the counterparty and can issue unlimited amounts. For MPT issuers with `shFULL_BALANCE`, the available issuance capacity (`MaximumAmount − OutstandingAmount`) is returned. For regular MPT holders, the `MPToken` ledger object's `sfMPTAmount` field is read, then conditionally zeroed based on freeze and authorization state. The freeze check (`fhZERO_IF_FROZEN`) and auth check (`ahZERO_IF_UNAUTHORIZED`) are independent policy inputs, allowing callers to compose them.

`accountFunds` differs from `accountHolds` in one key way: if the account *is* the IOU issuer, it returns `saDefault` directly (the amount requested), rather than an artificially large value. This is the correct semantic for offer matching — an issuer can always fund an offer for their own currency up to whatever amount they specify. The MPT overload of `accountFunds` delegates to `accountHolds` with `shFULL_BALANCE`.

`transferRate` dispatches on the `STAmount`'s embedded `Asset`, returning the issuer's transfer fee for IOU or the `MPTokenIssuance`'s fee for MPT, uniformly as a `Rate` (parts-per-billion).

## Holding Lifecycle

`canAddHolding` / `addEmptyHolding` / `removeEmptyHolding` manage the creation and deletion of the ledger objects that record a token relationship — trust lines for IOU, `MPToken` objects for MPT. The comment in `RippleStateHelpers.h` makes the protocol explicit: any transactor that calls `addEmptyHolding` in `doApply` must call `canAddHolding` in `preflight`, since the preflight check may reject the transaction before the apply phase writes to the ledger. `canAddHolding` for IOU verifies that the issuer has `lsfDefaultRipple` set; for MPT it delegates to the MPT-specific check.

## Authorization and Transfer Checks

`requireAuth` tests whether the account holds the required authorization to interact with the asset, returning `tecNO_AUTH` or `tecNO_LINE` on failure. `canTransfer` tests whether a particular sender→receiver pair is permitted: for IOU it checks rippling flags; for MPT it checks `lsfMPTCanTransfer` and authorization of the destination account.

## Money Transfer Functions

`directSendNoFee` is the primitive for redemption and intra-issuer transfers. It is intentionally not marked `[[nodiscard]]` — the comment explains this is for `DirectStep.cpp` compatibility — distinguishing it from the higher-level functions that enforce result checking.

`accountSend` is the main asset transfer entry point. It dispatches to `accountSendIOU` or `accountSendMPT` based on the `STAmount`'s asset type. Transfer fees are applied unless `WaiveTransferFee::Yes` is passed. The `AllowMPTOverflow` flag gates whether the MPT outstanding-amount overflow check uses the stricter `MaximumAmount` threshold or the relaxed `UINT64_MAX` threshold, matching the two-phase (issue then redeem) structure of the payment engine.

`accountSendMulti` handles the case where one sender distributes the same asset to multiple recipients simultaneously — used by vault operations and similar batch contexts. Batching avoids repeated round-trips through the ledger state for the sender's balance and the issuance's outstanding-amount field.

`transferXRP` is the primitive XRP send, kept separate because XRP has no trust lines, no transfer fees, and no authorization model; mixing it into the Asset-dispatch path would only add unnecessary overhead.