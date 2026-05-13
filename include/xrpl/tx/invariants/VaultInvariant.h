/** @file
 *  Post-transaction invariant checker for the SingleAssetVault feature.
 *
 *  Declares `ValidVault`, the dedicated guardian for vault ledger objects
 *  (`ltVAULT`, the share `ltMPTOKEN_ISSUANCE`, depositor `ltMPTOKEN` entries,
 *  vault pseudo-account `ltACCOUNT_ROOT`, and associated trust lines). It is
 *  registered as position 24 in the `InvariantChecks` tuple and runs after
 *  every transaction before ledger state is committed.
 */
#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <optional>
#include <unordered_map>
#include <vector>

namespace xrpl {

/** Post-transaction consistency guard for Single Asset Vaults.
 *
 *  Implements the two-phase invariant checker interface required by
 *  `InvariantChecks`. `visitEntry` accumulates per-entry balance deltas and
 *  snapshots vault/MPT state; `finalize` evaluates per-transaction invariants
 *  against those snapshots.
 *
 *  All meaningful work is skipped via an early-exit when no vault entry was
 *  touched, making this checker essentially free for the vast majority of
 *  transactions. `Number` (high-precision rational) is used throughout for
 *  asset amounts because IOU assets require fractional precision beyond what
 *  64-bit integers can represent losslessly.
 *
 *  Enforcement is gated on `featureSingleAssetVault`: violations are always
 *  logged at `fatal` level and fire a debug-build `XRPL_ASSERT`, but
 *  `finalize` only returns `false` (blocking the transaction) once the
 *  amendment is live on the network.
 *
 *  @see InvariantChecker_PROTOTYPE for the duck-typed interface contract.
 *  @see InvariantChecks in `InvariantCheck.h` for checker registration.
 */
class ValidVault
{
    Number static constexpr kZERO{};

    /** Immutable snapshot of a single `ltVAULT` ledger entry.
     *
     *  Captures every field that invariant checks compare across before/after
     *  states. Constructed exclusively via `Vault::make`; not an aggregate so
     *  that factory validation (the `XRPL_ASSERT` on entry type) is always
     *  enforced.
     */
    struct Vault final
    {
        uint256 key = beast::kZERO;
        Asset asset;
        AccountID pseudoId;
        AccountID owner;
        uint192 shareMPTID = beast::kZERO;
        Number assetsTotal = 0;
        Number assetsAvailable = 0;
        Number assetsMaximum = 0;
        Number lossUnrealized = 0;

        /** Construct a `Vault` snapshot from a live `ltVAULT` ledger entry.
         *
         *  @param from  A ledger entry whose type must be `ltVAULT`.
         *  @return      A fully-populated `Vault` value object.
         */
        Vault static make(SLE const&);
    };

    /** Immutable snapshot of a single `ltMPTOKEN_ISSUANCE` ledger entry.
     *
     *  Captures identity (`MPTIssue`), current outstanding amount, and
     *  effective maximum so `finalize` can check share-ceiling constraints.
     *  Because a modified `ltMPTOKEN_ISSUANCE` may belong to the vault or to
     *  an entirely unrelated MPT issuance, these are recorded lazily in
     *  `visitEntry` and resolved against `shareMPTID` in `finalize`.
     */
    struct Shares final
    {
        MPTIssue share;
        std::uint64_t sharesTotal = 0;
        std::uint64_t sharesMaximum = 0;

        /** Construct a `Shares` snapshot from a live `ltMPTOKEN_ISSUANCE` entry.
         *
         *  When `sfMaximumAmount` is absent the effective maximum defaults to
         *  `kMAX_MP_TOKEN_AMOUNT`.
         *
         *  @param from  A ledger entry whose type must be `ltMPTOKEN_ISSUANCE`.
         *  @return      A fully-populated `Shares` value object.
         */
        Shares static make(SLE const&);
    };

public:
    /** Signed balance delta for a single ledger entry, with precision metadata.
     *
     *  Pairs a `Number` delta with an optional scale (exponent) so that
     *  comparisons in `finalize` can round all operands to a common precision
     *  before testing equality. The scale is `std::nullopt` until the entry
     *  type is identified in `visitEntry`; a present scale of `0` means the
     *  value is an integer (XRP drops or MPT integer amount).
     *
     *  Sign convention (set in `visitEntry`): positive delta means assets or
     *  shares *flowed into* the associated account from the vault's perspective.
     *  For `ltMPTOKEN_ISSUANCE` the sign is `+1` (outstanding amount increases
     *  as shares are minted). For account roots, trust lines, and `ltMPTOKEN`
     *  the sign is `-1` (a balance decrease means assets left the holder).
     */
    struct DeltaInfo final
    {
        Number delta = kNUM_ZERO;
        std::optional<int> scale;

        /** Construct a `DeltaInfo` representing the change from `before` to `after`.
         *
         *  Sets `scale` to the coarser (larger) of the two values' asset-specific
         *  scales so that subsequent rounding via `roundToAsset` uses a precision
         *  no finer than either operand.
         *
         *  @param before  Pre-transaction numeric value.
         *  @param after   Post-transaction numeric value.
         *  @param asset   Asset type used to determine the appropriate scale for
         *      each value via `xrpl::scale()`.
         *  @return `DeltaInfo` where `delta = after - before` and `scale` is the
         *      maximum of the two operand scales.
         */
        [[nodiscard]] static DeltaInfo
        makeDelta(Number const& before, Number const& after, Asset const& asset);
    };

private:
    std::vector<Vault> afterVault_;
    std::vector<Shares> afterMPTs_;
    std::vector<Vault> beforeVault_;
    std::vector<Shares> beforeMPTs_;
    std::unordered_map<uint256, DeltaInfo> deltas_;

public:
    /** Phase-1 entry visitor: accumulate per-key balance deltas and snapshots.
     *
     *  Called once per modified ledger entry during transaction processing.
     *  For `ltVAULT` entries, pushes a `Vault` snapshot into `beforeVault_`
     *  and/or `afterVault_`. For `ltMPTOKEN_ISSUANCE` entries, pushes a
     *  `Shares` snapshot (vault vs. unrelated issuance is resolved later in
     *  `finalize`). For balance-carrying entries (`ltMPTOKEN_ISSUANCE`,
     *  `ltMPTOKEN`, `ltACCOUNT_ROOT`, `ltRIPPLE_STATE`), accumulates a signed
     *  `DeltaInfo` into `deltas_` keyed by ledger-entry key.
     *
     *  A delta entry is recorded even when the net balance change is zero
     *  (e.g., a fee exactly offsets an incoming transfer) to avoid treating a
     *  coincidental zero as "no activity".
     *
     *  @param isDelete  `true` when the entry is being deleted; the `after`
     *      snapshot is not pushed for deleted entries.
     *  @param before    Pre-transaction SLE, or `nullptr` for newly created entries.
     *  @param after     Post-transaction SLE; must be non-null.
     */
    void
    visitEntry(bool isDelete, std::shared_ptr<SLE const> const& before, std::shared_ptr<SLE const> const& after);

    /** Phase-2 invariant evaluation: verify vault consistency after transaction.
     *
     *  Called once after all `visitEntry` calls complete. Returns `true` if
     *  all relevant invariants pass (or the transaction did not touch any vault
     *  objects), `false` if a violation is detected and `featureSingleAssetVault`
     *  is active.
     *
     *  Short-circuits immediately on non-`tesSUCCESS` results. Per-transaction
     *  invariants checked include:
     *  - At most one vault is created or modified per transaction.
     *  - Immutable fields (`sfAsset`, `sfAccount`, `sfShareMPTID`) never change.
     *  - `assetsAvailable` ≤ `assetsTotal` ≥ 0; `assetsMaximum` ≥ 0.
     *  - `lossUnrealized` ≤ `assetsTotal − assetsAvailable`.
     *  - `lossUnrealized` changes only for `ttLOAN_MANAGE` / `ttLOAN_PAY`.
     *  - Asset and share conservation per transaction type (deposit, withdraw,
     *    clawback, set, create, delete).
     *  - Deletion co-deletes the share issuance and requires zero assets/shares.
     *  - Creation produces an empty vault whose pseudo-account back-links via
     *    `sfVaultID`.
     *  - Only transactions with `MustModifyVault` or `MayModifyVault` privilege
     *    may touch vault state at all.
     *
     *  @param tx    The applied transaction.
     *  @param ret   Final TER result of the transaction.
     *  @param fee   Transaction fee in drops (compensated in XRP-vault delta checks).
     *  @param view  Current ledger view for read-only fallback lookups.
     *  @param j     Journal for `fatal`-level invariant-failure diagnostics.
     *  @return `true` if invariants pass or are not applicable; `false` on violation.
     */
    bool
    finalize(STTx const& tx, TER const ret, XRPAmount const fee, ReadView const& view, beast::Journal const& j);

    /** Return the coarsest (largest) scale across a set of `DeltaInfo` values.
     *
     *  Invariant comparisons mix values computed at different precisions
     *  (e.g., XRP drops vs. IOU mantissa-exponent pairs). Rounding all operands
     *  to the coarsest scale prevents spurious equality failures caused by
     *  sub-precision differences.
     *
     *  @param numbers  Collection of `DeltaInfo` values, each carrying an
     *      optional scale. An absent scale indicates the value has not yet been
     *      assigned a precision context (should not occur in well-formed inputs).
     *  @return The maximum scale value found, or `0` if `numbers` is empty.
     */
    [[nodiscard]] static std::int32_t
    computeCoarsestScale(std::vector<DeltaInfo> const& numbers);
};

}  // namespace xrpl
